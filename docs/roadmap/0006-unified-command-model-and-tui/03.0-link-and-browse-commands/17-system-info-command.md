# 17 - system.info command

**Parent task:** 03.0 Link and browse commands on the registry
**State:** ⬜ Not started
**Depends on:** 01.0/05-07 (descriptor, `execute`, caller context); 02.0/10 (`--storage-config` / env flag: only then does a `StorageConfig` actually select the backend the commands use)
**Blocks:** 18, 06.0/17
**Estimated size:** S
**Suggested agent:** cpp-expert (security-auditor reviews in subtask 18)

## Objective

Implement `system.info`: `{version, backend:{metadata,cache,analytics,rate_limit}, persistent, read_only}`.
It is the TUI status-bar source and a cheap "which backend am I talking to" check. It must be impossible for
it to disclose a DSN, file path, address, key, password or salt, so it reports **backend names from a fixed
allowlist only** rather than redacting configuration values.

## Dependencies

- `StorageConfig` (verified, `include/url_shortener/config/storage_config.hpp`, global namespace):
  `metadata.backend` (default `inmemory`; also `sqlite`, `postgres`), `cache.backend` (default `none`; `redis`),
  `analytics.backend` (default `noop`), `rate_limit.enabled` + `rate_limit.backend` (default `inmemory`; `redis`).
  The same struct also holds `sqlite_path`, `postgres_dsn`, `redis_address`: those are **never** read here.
- Verified: `::observability::redactSecretValue(key, value)` (`storage_observability.hpp`, **global** namespace
  `observability`; inside `url_shortener::` it must be written with the leading `::`, because
  `url_shortener::observability` also exists for `Logger`) masks values only for keys containing `password`,
  `secret`, `token` or `dsn` (it does **not** match `key`, contrary to the CLAUDE.md summary; correction scheduled
  in Task 08.0 subtask 06). Not used here because nothing but allowlisted names is emitted.
- Version source: CMake `project(... VERSION 1.0.0)` (`CMakeLists.txt` line ~20) exists but no compile definition
  forwards it (verified: no `URL_SHORTENER_VERSION`/`PROJECT_VERSION` use in `src/`). This subtask adds one.
- `read_only` is `true` when the calling `ControlSet` cannot write (local `--read-only` downgrade, ADR 0002
  Authorization). Per 01.0/06 the handler reads it as `context.caller.control_set` (`CommandContext::caller`).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/app/system_info.hpp` | create | `SystemInfo`, `make_system_info(const StorageConfig&)`, `serializeSystemInfoJson`. |
| `src/app/system_info.cpp` | create | Allowlist normalization and serializer. Add to `COMMON_CPP_SOURCES`. |
| `include/url_shortener/commands/system_commands.hpp` | create | `make_system_info_descriptor(app::SystemInfo)`. |
| `src/commands/system_commands.cpp` | create | Descriptor/handler. Add to `COMMON_CPP_SOURCES`. |
| `CMakeLists.txt` | modify | `target_compile_definitions(url_shortener_common PUBLIC URL_SHORTENER_VERSION="${PROJECT_VERSION}")` after the `add_library(url_shortener_common ...)` (line ~105). |
| `tests/unit/app/48_system_info.cpp` | create | Normalization/serializer/redaction. |
| `tests/unit/commands/28_system_info_command.cpp` | create | Command behavior. |
| `sources.cmake` | modify | `APP_UNIT_SOURCES` += 48; `COMMANDS_UNIT_SOURCES` += 28; `COMMON_CPP_SOURCES` += two .cpp. |

## Classes and functions

```cpp
// namespace url_shortener::app
struct SystemInfo {
    std::string version;       // URL_SHORTENER_VERSION or "0.0.0-dev" if undefined
    std::string metadata;      // "inmemory" | "sqlite" | "postgres" | "unknown"
    std::string cache;         // "none" | "inmemory" | "redis" | "unknown"
    std::string analytics;     // "noop" | "inmemory" | "sqlite" | "postgres" | "unknown"
    std::string rate_limit;    // "disabled" | "inmemory" | "redis" | "unknown"
    bool persistent = false;   // metadata is sqlite or postgres
};

/// Reads only the four backend-name fields of StorageConfig and rate_limit.enabled.
SystemInfo make_system_info(const ::StorageConfig& config);

/// Process-wide snapshot used by build_command_runtime (subtask 18). Default = make_system_info(StorageConfig{}).
/// The bootstrap that parses --storage-config (02.0/11-12) sets it once at startup; reads are thread-safe.
void set_process_system_info(SystemInfo info);
SystemInfo process_system_info();

/// {"version":"..","backend":{"metadata":"..","cache":"..","analytics":"..","rate_limit":".."},
///  "persistent":bool,"read_only":bool}
std::string serializeSystemInfoJson(const SystemInfo& info, bool read_only);

// namespace url_shortener::commands
CommandDescriptor make_system_info_descriptor(app::SystemInfo info);   // by value: immutable snapshot
```

`system.info`: `permission = read`, `kind = sync`, `mutating = false`, no args. Any backend string outside its
allowlist becomes `"unknown"` (so a mis-set value such as a DSN in `backend` cannot leak). `read_only =
!can_write(caller control set)` using `canWrite(ControlSet)` (`security/control_set.hpp`, verified).

## Implementation steps

1. Add the CMake compile definition.
2. Implement `make_system_info` with a table of allowed names per field.
3. Implement the serializer with `jsonString`.
4. Add the descriptor and handler.
5. Tests; register; build; run.

## Commands

```powershell
cmake -S . -B cmake-build -G Ninja
cmake --build cmake-build --target app__48_system_info commands__28_system_info_command
ctest --test-dir cmake-build -R "^(app__48_system_info|commands__28_system_info_command)$" --output-on-failure
```

(Re-run the configure step: the new compile definition changes the cache.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/app/48_system_info.cpp` | `app__48_system_info` | unit | default `StorageConfig` -> `inmemory/none/noop/disabled`, `persistent=false`; sqlite and postgres metadata -> `persistent=true`; `rate_limit.enabled=false` always `disabled` even if backend is `redis`; **secret test:** a `StorageConfig` whose `sqlite_path`, `postgres_dsn` (`postgres://u:SECRETPW@h/db`), `redis_address` (`redis://:TOKEN@h`) are populated and whose `backend` strings contain a DSN-looking value -> serialized JSON contains none of `SECRETPW`, `TOKEN`, the path, `postgres://`, `redis://`, `salt`, `@`; unknown backend -> `unknown`; JSON is valid and key order fixed; `process_system_info()` equals the legacy snapshot before any `set_process_system_info`, equals the set value afterwards, and is safe under two concurrent readers while one writer sets it. |
| `tests/unit/commands/28_system_info_command.cpp` | `commands__28_system_info_command` | unit | success shape; `read_only:false` for `ControlSet::Admin`, `true` for `ControlSet::User`; unknown arg -> `invalid_field`; `dry_run:true` -> `unsupported`; result stable across calls (snapshot). |

Manual check: `url_shortener cmd system.info` (after Task 04.0) prints version and backend names, no paths.

## Constraints

- C6: output built only from allowlisted names, `bool`s and the version string; no config value pass-through.
- Never log or serialize `ServerConfig` (`analytics_client_hash_salt`, TLS paths and passphrase live there).
- Auth broker rules unchanged; this command exposes no auth information.
- C++17; no JSON library; naming per `.clang-tidy`; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. Both tests pass, including the secret test.
2. `grep -n "sqlite_path\|postgres_dsn\|redis_address" src/app/system_info.cpp` is empty.
3. `url_shortener_common` compiles with `URL_SHORTENER_VERSION` defined and the output contains `1.0.0`.

## Out of scope / risks

- Per-backend health (`/readyz`) is observability, not a command.
- If a future backend is added, its name must be added to the allowlist (test forces the explicit decision by
  asserting unknown -> `unknown`).
