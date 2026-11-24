# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A C++17 single-binary async HTTP/HTTPS server (Boost.Asio + Boost.Beast + OpenSSL) implementing a low-latency URL shortener. The redirect path (`GET /{slug}`, `GET /r/{slug}`) is a performance-sensitive fast path; everything else is the `/api/v1/links` management plane. See `ARCHITECTURE.md` and `docs/adr/` (ADR 0001–0003) for the normative design; `README.md` has full per-platform build instructions and API examples. External operations are JSON commands (`docs/commands/README.md`, ADR 0002) carried by REST, CLI, `serve-stdio`, and an optional FTXUI TUI (ADR 0003).

## Build

Standard out-of-source CMake + Ninja build:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja
cmake --build .
```

Binary output: `cmake-build/url_shortener`.

- `yaml-cpp`, `hiredis`, and `SOCI` (SQLite + PostgreSQL backends) are fetched and built automatically via CMake `FetchContent` — do not install them manually.
- `-DURLSHORTENER_ENABLE_TUI=ON|OFF` (default `ON`): builds FTXUI and `src/tui/`; FTXUI is fetched via `FetchContent`. Everything outside `src/tui/` builds without it.
- On Windows, vcpkg is mandatory: CMake fails fast unless `CMAKE_TOOLCHAIN_FILE` or `VCPKG_ROOT` is set (see README for the full vcpkg command).
- On macOS, pick exactly one dependency strategy (Homebrew/MacPorts/vcpkg) — never mix, since duplicate Boost/OpenSSL installs break CMake discovery. Homebrew requires `-DCMAKE_PREFIX_PATH=...` for the keg-only `openssl@3`/`libpq`/`sqlite` formulas; CMake 4.x on Homebrew Boost may also need `-DBoost_NO_BOOST_CMAKE=ON`.
- **All `.cpp` files compiled into `url_shortener_common` must be listed in `sources.cmake`.** This includes `commands/`, `transport/`, and (guarded) `tui/` sources. Adding a new source file without updating `sources.cmake` means it silently isn't built.

## Tests

Tests are registered via `add_test` and grouped by CTest labels: `unit`, `contract` (backend-agnostic suites run against any `IMetadataRepository` impl), `integration`, `e2e` (shell scripts under `tests/e2e/scripts/`, POSIX-only, toggle with `-DURLSHORTENER_ENABLE_E2E_TESTS=OFF`), `perf` (registered via `PERF_SOURCES` in `sources.cmake`), and `conformance` (cross-transport corpus under `tests/conformance/`).

Always pass `--test-dir` pointing at the build directory:

```bash
# everything
ctest --test-dir cmake-build --output-on-failure

# one label
ctest --test-dir cmake-build -L unit --output-on-failure

# conformance suite
ctest --test-dir cmake-build -L conformance --output-on-failure

# all labels via union regex
ctest --test-dir cmake-build -L "unit|contract|integration|e2e|conformance" --output-on-failure

# a single test by exact name
ctest --test-dir cmake-build -R "^<test_name>$" --output-on-failure
```

On Windows/multi-config generators add `-C Debug` (or the relevant config) to every `ctest`/`cmake --build` invocation.

**Test target naming** (needed to `cmake --build` a single test before running it):
- Tests in `CORE_BASIC_UNIT_SOURCES` / `LINK_MANAGEMENT_UNIT_SOURCES` / `HTTP_UNIT_SOURCES` (defined in `sources.cmake`, registered in `CMakeLists.txt`) → target name is the filename stem, e.g. `01_url_validation_accept_test`.
- Tests registered via `register_labeled_cpp_tests(...)` → target name is `<parent_dir>__<filename_stem>`, e.g. `inmemory__01_create_and_get_roundtrip`, `commands__07_execute_pipeline`, `tui__04_frame_model_test`, `conformance__01_direct_registry_corpus`.
- Python conformance tests → CTest name is `conformance_<stem>`, e.g. `conformance_10_cli_cmd_corpus`.
- Docs drift tests → `docs__<stem>`, e.g. `docs__03_command_catalog_matches_registry`.

**Adding a new test:**
- C++ unit tests for the command layer go in `tests/unit/commands/`; TUI tests in `tests/unit/tui/` (goldens in `tests/unit/tui/golden/`).
- Conformance corpus cases are JSON files in `tests/conformance/commands/`; run `python tools/conformance_lint.py <file>` before committing, and add transport-agnostic cases before handler-specific tests.
- For other suites: add the `.cpp` under the appropriate `tests/unit|integration|contract/<subdir>/` directory with a numeric prefix, then append its path to the matching source list in `sources.cmake`.
- `UPDATE_GOLDEN=1` regenerates TUI render goldens locally; never committed via CI.
- For per-task test prefix ranges and CMake list owners, see ["Test file prefixes and CMake list ownership"](docs/roadmap/0006-unified-command-model-and-tui/plan.md#test-file-prefixes-and-cmake-list-ownership) in the milestone plan.

Integration tests also include a Python harness (`tests/integration/py/`) and `tools/sqlite_state_assert.py` for asserting SQLite state directly.

## High-level architecture

### Runtime data flow

1. `src/main.cpp` parses CLI flags into `ServerConfig`, initializes `io_context`, handles `SIGINT`/`SIGTERM`/`SIGHUP`.
2. `composition::InitLinkBackend` selects the storage backend and installs it as the process-wide `app::LinkBackend::store()`.
3. `HttpServer` (`src/http/`) accepts connections and spawns a `PlainSession` or `TlsSession` per connection. `handleShortenerRequest` delegates to the application `Router`; `RouterBuilder` registers handlers (from `src/http/handlers/`) in priority order: observability → canonical `/api/v1/links` → compatibility `/api/v1/short-urls` → redirects → generic URI-store fallback. `RouteRegistry` is the C++ source of truth for route metadata and generates `docs/api/README.md`. Resource and generic command routes call `CommandRegistry::execute`.
4. Redirect resolution reads `app::LinkBackend::store()` directly (the fast path). **New redirect behavior belongs in `ILinkStore` and its implementations, not in request handlers.**
5. `BoundedClickEventQueue` receives analytics events non-blocking (drop-on-overflow); a background `AnalyticsWorker` drains it to `IClickEventRepository`.

### Command model

`CommandRegistry` (`src/commands/`) is the single entry point for all business behavior:
validate envelope → authorize (`AccessGuard`) → `ArgSpec` check → handler →
`app::LinkCommandService` / browse / pair services. **New commands and behavior changes go here, never in transports.**

Key facts:
- One command catalog (`docs/commands/README.md`): block `### 3.7` is generated by `command_catalog_dump`; edit descriptors first, then regenerate.
- `command.list` returns all registered descriptors at runtime.
- Jobs (`link.import`, `link.export`): one process-wide `JobRunner`, in-memory state, `busy` guard. Job IDs start with `j-`.
- One composition root: `composition::build_command_runtime`; no mode builds its own registry (contract C11).
- Transports implement `ITransportAdapter`; see `docs/commands/adding-a-transport.md`.
- `db.*` table allowlist: `links`, `click_events`, `jobs`; no auth tables, no secrets.
- Local transports (`cmd`, `serve-stdio`, `tui`) run as local operator (Admin role by default); `--read-only` downgrades to `ControlSet::User`.
- The REST command API is off by default; enable with `--command-api-enabled` and control role with `--command-api-role`.
- The one sanctioned JSON tokenizer for the command layer: `commands/json_scan.hpp`.

### Storage: ports and adapters

Depend on interfaces, never concrete types:

| Interface | Responsibility |
|---|---|
| `IMetadataRepository` | Link CRUD: `CreateLink`, `GetByShortCode`, `UpdateLink`, `DeleteLink`, `ListLinks`, `Exists` |
| `ICacheStore` | Cache-aside: `Get`, `Set` (optional TTL), `Delete`, `ClearByPrefix` |
| `IRateLimiter` | `Allow(key, limit, window)` → `RateLimitDecision` |
| `IAnalyticsSink` | `Emit(event)`, `Flush()` — called in the redirect hot path |
| `IClickEventRepository` | Persistence + aggregate reads for the analytics worker |

Backends: in-memory (`storage/inmemory/`, default, no external deps), SQL (`storage/sql/`, backend-neutral layer over SOCI shared by SQLite/Postgres via `SqlDialect`), SQLite (`storage/sqlite/`), PostgreSQL (`storage/postgres/`, with `PostgresMigrationRunner` and numbered migrations in `db/migrations/postgres/`), Redis (`storage/redis/`, via `hiredis`, non-authoritative/fail-open).

`composition::InitLinkBackend` wires these into an `app::ILinkStore` shared by redirects, REST, CLI, and TUI.

**Cache-aside contract:** `ICacheStore::Get` first → on miss/failure fall back to `IMetadataRepository::GetByShortCode` → populate cache best-effort → invalidate cache on `UpdateLink`/`DeleteLink` → cache failures never fail the request.

Errors are never thrown for expected conditions (`not_found`, `already_exists`, etc.) — methods return `bool`/`std::optional<T>` and write to an optional `T* error` out-param. Exceptions are reserved for programming errors and unrecoverable startup failures.

To add a new storage backend, see `docs/storage/how-to-add-adapter.md` (implement interfaces → map errors → register in config/factory → contract tests → backend unit + integration tests → CI).

### Analytics pipeline

`RedirectAnalyticsHook` → `AnalyticsService::RecordRedirectAttempt` → `BoundedClickEventQueue::TryEnqueue` → `AnalyticsWorker` (background thread, batches + retries) → `IClickEventRepository`. `ClientIdHasher` produces an HMAC-SHA256 `client_id_hash`; raw client identifiers are never stored. `ClickEventSanitizer` length-bounds `referrer`/`user_agent`/`domain` before enqueue. The entire pipeline is best-effort — queue overflow is silent, and analytics failures must never affect redirect response behavior or latency.

### Security (`security/`)

Auth is **local-only** — the auth broker must never be exposed to the public internet (Unix domain socket or loopback HTTP only). The HTTP server always binds `0.0.0.0`; there is no bind-address flag yet. The command API grants Admin to loopback peers, so deployments must firewall the port. Two roles via `ControlSet`: `Admin` (read/write/migrate/manage users) and `User` (read only); `AccessGuard::requireRead/requireWrite/requireMigration/requireUserManagement()` throw `AccessDeniedError` if absent. Passwords are PBKDF2-SHA256 (100k iterations); only token *hashes* (SHA-256) are persisted, raw tokens are never stored or logged; failed logins return generic errors; all auth operations go to `auth_audit_log`. SSRF host rules: underscores, `%`, `\`, and control bytes are refused; IDN hosts must be punycoded before submission.

### Config and wiring

- `ServerConfig` (`core/config.hpp`) — top-level runtime config from CLI flags (`src/cli_parser.cpp`): ports, TLS, `shortener_base_domain`, `shortener_generated_slug_length`, analytics settings, request hardening limits, command API flags.
- `StorageConfig` — four independent backend sections (`metadata`, `cache`, `analytics`, `rate_limit`), parsed from YAML via `ParseStorageConfigFile`/`ParseStorageConfigYaml` (throws `std::runtime_error` on invalid config).
- Key env vars: `SHORTENER_BASE_DOMAIN`, `SHORTENER_DEFAULT_REDIRECT_TYPE`, `SHORTENER_GENERATED_SLUG_LENGTH`, `SHORTENER_ALLOW_PRIVATE_TARGETS`, `URL_SHORTENER_ANALYTICS_HASH_SALT`.

### Test utilities

- `ManualClock` (`core/clock.hpp`) — inject instead of `SystemClock` for deterministic time-based tests (`clock.advance(std::chrono::seconds(3600))`).
- Contract tests (`tests/contract/metadata/`) are run against every `IMetadataRepository` implementation, including via `tests/integration/sqlite/03_contract_suite_runner.cpp` and `tests/integration/psql/05_contract_suite_runner.cpp`.

## Key conventions

- **C++17 only** — do not use C++20 features.
- **Naming** (enforced by `.clang-tidy`): everything `lower_case` except template parameters (`CamelCase`) and macros (`UPPER_CASE`). Private/protected members use `m_` prefix (e.g. `m_mutex_`); struct fields and public members are plain `lower_case`.
- **Formatting** (enforced by `.clang-format`): 4-space indent, 80 columns, braces on their own line for class/function/enum/struct definitions, includes grouped std → system → local.
- **Doxygen completeness:** every class/struct/interface, method/function, enum/enumerator, template parameter, and non-obvious struct field carries a complete Doxygen comment — not just a `@brief` that restates the name, but `@param`/`@return` (including every failure case for the `bool`+`*error`-out-param convention below) and the lifetime contract for any reference/pointer-capturing constructor. See `.claude/skills/doxygen-documentation/SKILL.md` for templates and worked examples; `cpp-code-documenter` runs dedicated backfill passes on legacy modules.
- **JSON parsing:** manual string-scanning helpers in `core/utils.hpp` (`extractJsonStringField`, `extractJsonBoolField`, etc.) — no JSON library dependency. Use these, don't add a new JSON library. The command layer's JSON tokenizer is `commands/json_scan.hpp`; use it there.
- **Redirect fast path is protected:** `GET /{slug}` and `GET /r/{slug}` must stay to `ILinkStore` lookup + redirect response. Do not add management-plane logic here — that belongs under `/api/v1/links`. Redirect handlers must not include command-layer headers (enforced by `commands__80_redirect_isolation_test`).
- **Transports are thin:** transports contain no per-command logic, DTO mapping, or error mapping (contract C1). TUI code only talks to `ICommandClient`; no service, repository, or DTO includes under `src/tui/` (contract C8, enforced by `tui__02_include_boundary_test`). CLI/TUI/stdio modes never start `HttpServer` or bind a socket (contract C5, tested in `conformance_32_no_listener_process_level`).
- **API shape:** canonical resource is `/api/v1/links/{slug}`; `/api/v1/short-urls/...` and `/r/{slug}` are compatibility aliases. New endpoints target `/api/v1/links/`.
- Treat all user-supplied URLs, slugs, and request bodies as untrusted (SSRF via private targets — see `isPrivateHost`/`SHORTENER_ALLOW_PRIVATE_TARGETS`, oversized bodies — see request hardening limits below).
- Never log secrets, TLS private keys, DSNs, tokens, or the analytics salt. `observability::redactSecretValue` (in `observability/storage_observability.hpp`) redacts keys containing `"password"`, `"secret"`, or `"token"`, and redacts only the password segment of DSN values; test code that builds fake secrets at runtime does not violate this.
- Request hardening defaults (CLI-configurable): `--max-request-body-bytes` (65536), `--max-request-target-length` (2048), `--request-id-max-length` (64).
- TLS defaults: minimum TLS 1.2, strong ciphers/curves, `session_tickets = false` by default; mTLS via `client_auth_mode` (`none`/`optional`/`required`); context reloads on `SIGHUP` without restart.

## Delegating to specialized agents

This repo defines specialized subagents in `.claude/agents/` for a design → implement → test → review → docs workflow. Prefer delegating rather than doing everything inline:

- **app-architect** — design decisions, ADRs, interface contracts (does not write code).
- **cpp-expert** — all implementation in `src/`, `include/`, `tests/`. For command/descriptor work, read `docs/commands/README.md` and `docs/commands/adding-a-transport.md` first.
- **testing-expert** — test generation and coverage analysis; add conformance corpus cases before handler tests. **test-documenter** — Boost.Test Scenario/Boundaries doc comments on existing tests.
- **cpp-code-documenter** — brings existing C++ entity declarations up to full Doxygen completeness (parameters, return/error semantics, ownership/thread-safety) on a legacy module or dedicated backfill pass; not a substitute for `cpp-expert` writing docs on new code as it's authored.
- **feature-reviewer** + **security-auditor** — review gate before merge (run together via the `/pr-review` skill).
- **subtask-verifier** — checks an implementation against its spec in `docs/specs/` / `docs/agent/implementation-taskmap.md`.
- **docs-writer** (net-new docs) / **docs-updater** (keeping existing docs in sync with code changes).
- **agent-orchestrator** — coordinates the full sequence for multi-step work spanning several of the above.

Useful skills: `/pr-review`, `/test-gap`, `/verify-subtask`, `/adr-write`, `/document-tests`, `/doxygen-documentation`, `/doc-xref`, `/doc-registry`, `/link-check`, `/secret-scan`.
