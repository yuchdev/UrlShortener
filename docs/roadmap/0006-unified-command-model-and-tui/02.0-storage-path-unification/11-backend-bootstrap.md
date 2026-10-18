# 11 - Backend bootstrap

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/04, 02.0/05, 02.0/07, 02.0/10
**Blocks:** 12
**Estimated size:** M
**Suggested agent:** cpp-expert (review: security-auditor, secrets handling)

## Objective

Add the one function that turns `ServerConfig::storage_config_path` into a live,
installed backend: parse the YAML, `BuildStorageAdapters`, construct the
`RepositoryLinkStore` with the right cache TTL and stats-flush interval, probe
the backend so a bad path or unreachable database fails at startup, and install
it into `LinkBackend`. It returns an RAII handle that keeps the adapters alive
and flushes pending redirect counters on destruction. Called from `main`
(subtask 12) before any server or CLI work; a no-op when no path is configured.

## Dependencies

- `ParseStorageConfigFile` (`include/url_shortener/config/storage_config.hpp`,
  verified; throws `std::runtime_error` on invalid configuration, yaml-cpp
  exceptions on unreadable/invalid YAML).
- `BuildStorageAdapters(const StorageConfig&, const IClock&)` and
  `StorageAdapters {metadata, cache, analytics, rate_limiter}`
  (`include/url_shortener/composition/storage_factory.hpp`, verified).
  Behavior verified in `storage_factory.cpp`: `inmemory` -> `InMemoryMetadataRepository`;
  `sqlite` -> `SqlMetadataRepository` over `SqliteSessionFactory`; `postgres`
  likewise; cache `none|inmemory|redis`; the SQL schema is created lazily on the
  first operation (`EnsureBootstrapped`), so construction alone does not open the
  database file.
- 02.0/03-05 store, 02.0/01 cache, 02.0/07 holder, 02.0/10 `storage_config_path`.
- `SystemClock` (`core/clock.hpp`).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/composition/link_backend_bootstrap.hpp` | create | Handle type and `InitLinkBackend` |
| `src/composition/link_backend_bootstrap.cpp` | create | Implementation |
| `tests/integration/storage/15_link_backend_bootstrap.cpp` | create | Bootstrap tests (SQLite temp files) |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/composition/link_backend_bootstrap.cpp`; `STORAGE_INTEGRATION_SOURCES` += test (registered via `register_labeled_cpp_tests(... "integration")`, CMakeLists.txt:189; `CONFIG_INTEGRATION_SOURCES` is never registered, see the 02.0 README) |

Target of the test: `storage__15_link_backend_bootstrap` (label integration).

## Classes and functions

```cpp
namespace url_shortener::app {

struct LinkBackendSettings
{
    std::string metadata_backend;                 // "inmemory" | "sqlite" | "postgres"
    std::string cache_backend;                    // "none" | "inmemory" | "redis"
    std::chrono::seconds l1_ttl{0};               // 0 for inmemory metadata, 5 s otherwise
    std::chrono::seconds stats_flush_interval{0}; // 0 for inmemory metadata, 30 s otherwise
};

class LinkBackendHandle
{
public:
    LinkBackendHandle() = default;                 // inactive
    LinkBackendHandle(LinkBackendHandle&&) noexcept;
    LinkBackendHandle& operator=(LinkBackendHandle&&) noexcept;
    LinkBackendHandle(const LinkBackendHandle&) = delete;
    ~LinkBackendHandle();                          // stop_stats_flusher() (joins + final flush);
                                                   // keeps LinkBackend installed until reset()

    bool active() const;
    const LinkBackendSettings& settings() const;
    std::string summary() const;                   // summarize_storage_config of the active config
    void flush();                                  // RepositoryLinkStore::flush_stats(); shutdown/tests only
private:
    StorageAdapters m_adapters;
    std::shared_ptr<IClock> m_clock;
    std::shared_ptr<RepositoryLinkStore> m_store;
    LinkBackendSettings m_settings;
};

/// "metadata=<b> cache=<b> analytics=<b> rate_limit=<b|off>": backend names only.
/// Pure function, safe to log; never includes sqlite_path, DSN or addresses.
std::string summarize_storage_config(const StorageConfig& config);

/// No-op (inactive handle, nothing installed) when config.storage_config_path
/// is unset. Otherwise: parse -> build -> probe -> install.
/// Throws std::runtime_error with a message that never contains DSNs,
/// passwords or file contents: "storage config: <reason>" for parse/validation
/// problems, "storage backend unavailable" for a failed probe.
LinkBackendHandle InitLinkBackend(const ServerConfig& config);
}
```

Steps inside `InitLinkBackend`:

1. If `!config.storage_config_path` return an inactive handle.
2. `StorageConfig cfg = ParseStorageConfigFile(*path)`; catch
   `YAML::BadFile` -> "storage config: cannot read file"; other exceptions ->
   "storage config: " + `e.what()` (yaml-cpp and `Require(...)` messages contain
   positions and field names, not values; the invalid-backend message echoes the
   backend name only).
3. `clock = std::make_shared<SystemClock>()`; `adapters = BuildStorageAdapters(cfg, *clock)`.
4. Probe: `adapters.metadata->Exists("health-probe-slug", &err)`; `err` other
   than `none`/`not_found` -> throw "storage backend unavailable". This forces
   the SQLite file to be created/opened and the schema bootstrapped, and surfaces
   a wrong path or unreachable Postgres immediately.
5. Settings: in-memory metadata -> `l1_ttl = 0`, `stats_flush_interval = 0`;
   otherwise 5 s / 30 s (constants in the `.cpp`; env/flag tuning is a possible
   follow-up).
6. `l1 = std::make_shared<ExpiringLinkCache>(*clock, settings.l1_ttl)`;
   `store = std::make_shared<RepositoryLinkStore>(adapters.metadata,
   adapters.cache, l1, *clock, {settings.stats_flush_interval, ...})`;
   if `stats_flush_interval > 0`, `store->start_stats_flusher()` (the background
   thread of 02.0/05; redirect requests never persist counters themselves);
   `LinkBackend::install(store)`.
7. Return the handle (its destructor calls `stop_stats_flusher()`, which joins
   the thread and performs the final flush).

`analytics` and `rate_limiter` adapters are built (the factory does that
unconditionally) but unused by links in this task (README R-3); the handle keeps
them alive so later tasks can reuse them.

## Implementation steps

1. Header and source as above.
2. Integration test with temp YAML files and SQLite files.
3. Register; build; run.

## Commands

```powershell
cmake --build cmake-build --target storage__15_link_backend_bootstrap
ctest --test-dir cmake-build -R "^storage__15_link_backend_bootstrap$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/integration/storage/15_link_backend_bootstrap.cpp` | `storage__15_link_backend_bootstrap` | integration | below |

- No path -> `active() == false`, `LinkBackend::installed() == false`.
- `metadata: {backend: inmemory}` -> active, installed, settings TTL/flush 0;
  create + find through `LinkBackend::store()` work.
- SQLite temp file: create via the installed store, destroy the handle,
  `LinkBackend::reset()`, run `InitLinkBackend` again with the same YAML ->
  the link is found (persistence across "processes"); settings 5 s / 30 s.
- Missing YAML file -> `std::runtime_error` whose message starts with
  `storage config:` and does not contain the file's contents.
- Invalid backend name (`backend: foo`) -> runtime_error naming the backend, not
  the whole file.
- SQLite path inside a nonexistent directory -> `storage backend unavailable`
  (probe), nothing left installed (`installed() == false` after the throw).
- Redaction: `summarize_storage_config` for a `StorageConfig` built in memory
  (no connection) whose `postgres_dsn` carries a recognizable password token
  contains neither the DSN, the host nor that token; same for `sqlite_path` and
  a Redis address. `LinkBackendHandle::summary()` equals it for the active
  config.
- Destructor flushes: `recordRedirect` then handle destruction persists the
  counter (reopen and read); the flusher thread is joined (no thread left
  running after the handle is gone).
- Flusher started only for persistent metadata: SQLite handle -> flusher
  running; in-memory handle -> no thread (write-through).
- Move semantics: moving an active handle keeps exactly one flush on destruction.
- Cleanup: every case calls `LinkBackend::reset()` and removes temp files.

## Constraints

- Never log or throw DSNs, passwords, TLS keys, tokens (CLAUDE.md security
  rules); messages carry backend names and fixed phrases only.
  `::observability::redactSecretValue` is applied by any caller that prints config
  values (not needed for `summary()`).
- No listener, no `uri.txt` access (C5): this function is safe to call in CLI
  mode.
- Fail fast at startup for unrecoverable backend problems; transient problems
  later surface per request as `storage_failure` (cache failures still never fail
  a request).
- Default behavior unchanged without a path.
- C++17; handle is move-only RAII.

## Success criteria

1. `storage__15_link_backend_bootstrap` passes.
2. After a thrown error nothing is installed (test).
3. The persistence-across-`InitLinkBackend` case passes, which is the property
   the CLI integration tests (13) rely on.

## Out of scope / risks

- `RedisCacheStore` / Postgres constructor behavior when the service is down is
  UNVERIFIED (the factory builds them eagerly); the probe covers metadata only.
  Redis is non-authoritative and fail-open per CLAUDE.md, but a constructor that
  blocks would block startup; check in the Redis/Postgres follow-up tests.
- Tuning knobs for TTL and flush interval (env/YAML keys) are deliberately not
  added; they would change `StorageConfig` parsing and its docs.
- Postgres migrations (`PostgresMigrationRunner`) are not run here; schema
  management for Postgres stays as documented in `docs/storage/adapters/postgres.md`.
