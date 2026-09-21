# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A C++17 single-binary async HTTP/HTTPS server (Boost.Asio + Boost.Beast + OpenSSL) implementing a low-latency URL shortener. The redirect path (`GET /{slug}`, `GET /r/{slug}`) is a performance-sensitive fast path; everything else is the `/api/v1/links` management plane. See `ARCHITECTURE.md` and `docs/adr/` (ADR 0001–0005) for the normative design; `README.md` has full per-platform build instructions and API examples.

## Build

Standard out-of-source CMake + Ninja build:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja
cmake --build .
```

Binary output: `cmake-build/url_shortener`.

- `yaml-cpp`, `hiredis`, and `SOCI` (SQLite + PostgreSQL backends) are fetched and built automatically via CMake `FetchContent` — do not install them manually.
- On Windows, vcpkg is mandatory: CMake fails fast unless `CMAKE_TOOLCHAIN_FILE` or `VCPKG_ROOT` is set (see README for the full vcpkg command).
- On macOS, pick exactly one dependency strategy (Homebrew/MacPorts/vcpkg) — never mix, since duplicate Boost/OpenSSL installs break CMake discovery. Homebrew requires `-DCMAKE_PREFIX_PATH=...` for the keg-only `openssl@3`/`libpq`/`sqlite` formulas; CMake 4.x on Homebrew Boost may also need `-DBoost_NO_BOOST_CMAKE=ON`.
- **All `.cpp` files compiled into `url_shortener_common` must be listed in `sources.cmake`.** Adding a new source file without updating `sources.cmake` means it silently isn't built.

## Tests

Tests are registered via `add_test` and grouped by CTest labels: `unit`, `contract` (backend-agnostic suites run against any `IMetadataRepository` impl), `integration`, `e2e` (shell scripts under `tests/e2e/scripts/`, POSIX-only, toggle with `-DURLSHORTENER_ENABLE_E2E_TESTS=OFF`), and `perf`.

Always pass `--test-dir` pointing at the build directory:

```bash
# everything
ctest --test-dir cmake-build --output-on-failure

# one label
ctest --test-dir cmake-build -L unit --output-on-failure

# all labels via union regex
ctest --test-dir cmake-build -L "unit|contract|integration|e2e" --output-on-failure

# a single test by exact name
ctest --test-dir cmake-build -R "^<test_name>$" --output-on-failure
```

On Windows/multi-config generators add `-C Debug` (or the relevant config) to every `ctest`/`cmake --build` invocation.

**Test target naming** (needed to `cmake --build` a single test before running it):
- Tests in `CORE_BASIC_UNIT_SOURCES` / `LINK_MANAGEMENT_UNIT_SOURCES` (in `CMakeLists.txt`) → target name is the filename stem, e.g. `01_url_validation_accept_test`.
- Tests registered via `register_labeled_cpp_tests(...)` → target name is `<parent_dir>__<filename_stem>`, e.g. `inmemory__01_create_and_get_roundtrip`.

**Adding a new test:** add the `.cpp` under `tests/unit|integration|contract/<subdir>/` with a numeric prefix (`01_...`, `02_...`) matching sibling tests, then append its path to the matching source list in `CMakeLists.txt`.

Integration tests also include a Python harness (`tests/integration/py/`) and `tools/sqlite_state_assert.py` for asserting SQLite state directly.

## High-level architecture

### Runtime data flow

1. `src/main.cpp` parses CLI flags into `ServerConfig`, initializes `io_context`, handles `SIGINT`/`SIGTERM`/`SIGHUP`.
2. `HttpServer` (`src/http/`) accepts connections and spawns a `PlainSession` or `TlsSession` per connection.
3. `handleShortenerRequest` delegates to the application `Router`; `RouterBuilder` registers handlers (from `src/http/handlers/`) in priority order: observability → canonical `/api/v1/links` → compatibility `/api/v1/short-urls` → redirects → generic URI-store fallback. `RouteRegistry` is the C++ source of truth for route metadata and generates `docs/api/README.md`.
4. `LinkService` (`src/core/`) coordinates redirect resolution through `IMetadataRepository`, `ICacheStore`, and `IAnalyticsSink`. **New redirect behavior belongs here, not in request handlers.**
5. `BoundedClickEventQueue` receives analytics events non-blocking (drop-on-overflow); a background `AnalyticsWorker` drains it to `IClickEventRepository`.
6. `StorageFactory`/`BuildStorageAdapters` (`src/composition/`) is the single point where backend selection happens, constructing all four storage adapters from `StorageConfig` at startup.

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

**Cache-aside contract:** `ICacheStore::Get` first → on miss/failure fall back to `IMetadataRepository::GetByShortCode` → populate cache best-effort → invalidate cache on `UpdateLink`/`DeleteLink` → cache failures never fail the request.

Errors are never thrown for expected conditions (`not_found`, `already_exists`, etc.) — methods return `bool`/`std::optional<T>` and write to an optional `T* error` out-param. Exceptions are reserved for programming errors and unrecoverable startup failures.

To add a new storage backend, see `docs/storage/how-to-add-adapter.md` (implement interfaces → map errors → register in config/factory → contract tests → backend unit + integration tests → CI).

### Analytics pipeline

`RedirectAnalyticsHook` → `AnalyticsService::RecordRedirectAttempt` → `BoundedClickEventQueue::TryEnqueue` → `AnalyticsWorker` (background thread, batches + retries) → `IClickEventRepository`. `ClientIdHasher` produces an HMAC-SHA256 `client_id_hash`; raw client identifiers are never stored. `ClickEventSanitizer` length-bounds `referrer`/`user_agent`/`domain` before enqueue. The entire pipeline is best-effort — queue overflow is silent, and analytics failures must never affect redirect response behavior or latency.

### Security (`security/`)

Auth is **local-only** — the auth broker must never be exposed to the public internet (Unix domain socket or loopback HTTP only, never `0.0.0.0`). Two roles via `ControlSet`: `Admin` (read/write/migrate/manage users) and `User` (read only); `AccessGuard::requireRead/requireWrite/requireMigration/requireUserManagement()` throw `AccessDeniedError` if absent. Passwords are PBKDF2-SHA256 (100k iterations); only token *hashes* (SHA-256) are persisted, raw tokens are never stored or logged; failed logins return generic errors regardless of whether username or password was wrong; all auth operations go to `auth_audit_log`.

### Config and wiring

- `ServerConfig` (`core/config.h`) — top-level runtime config from CLI flags (`src/cli_parser.cpp`): ports, TLS, `shortener_base_domain`, `shortener_generated_slug_length`, analytics settings, request hardening limits.
- `StorageConfig` — four independent backend sections (`metadata`, `cache`, `analytics`, `rate_limit`), parsed from YAML via `ParseStorageConfigFile`/`ParseStorageConfigYaml` (throws `std::runtime_error` on invalid config).
- Key env vars: `SHORTENER_BASE_DOMAIN`, `SHORTENER_DEFAULT_REDIRECT_TYPE`, `SHORTENER_GENERATED_SLUG_LENGTH`, `SHORTENER_ALLOW_PRIVATE_TARGETS`, `URL_SHORTENER_ANALYTICS_HASH_SALT`.

### Test utilities

- `ManualClock` (`core/clock.hpp`) — inject into `LinkService` and storage adapters instead of `SystemClock` for deterministic time-based tests (`clock.advance(std::chrono::seconds(3600))`).
- Contract tests (`tests/contract/metadata/`) are run against every `IMetadataRepository` implementation, including via `tests/integration/sqlite/03_contract_suite_runner.cpp` and `tests/integration/psql/05_contract_suite_runner.cpp`.

## Key conventions

- **C++17 only** — do not use C++20 features.
- **Naming** (enforced by `.clang-tidy`): everything `lower_case` except template parameters (`CamelCase`) and macros (`UPPER_CASE`). Private/protected members use `m_` prefix (e.g. `m_mutex_`); struct fields and public members are plain `lower_case`.
- **Formatting** (enforced by `.clang-format`): 4-space indent, 80 columns, braces on their own line for class/function/enum/struct definitions, includes grouped std → system → local.
- **JSON parsing:** manual string-scanning helpers in `core/utils.h` (`extractJsonStringField`, `extractJsonBoolField`, etc.) — no JSON library dependency. Use these, don't add a new JSON library.
- **Redirect fast path is protected:** `GET /{slug}` and `GET /r/{slug}` must stay to cache lookup + repository fallback + redirect response. Do not add management-plane logic here — that belongs under `/api/v1/links`.
- **API shape:** canonical resource is `/api/v1/links/{slug}`; `/api/v1/short-urls/...` and `/r/{slug}` are compatibility aliases. New endpoints target `/api/v1/links/`.
- Treat all user-supplied URLs, slugs, and request bodies as untrusted (SSRF via private targets — see `isPrivateHost`/`SHORTENER_ALLOW_PRIVATE_TARGETS`, oversized bodies — see request hardening limits below).
- Never log secrets, TLS private keys, DSNs, tokens, or the analytics salt. `StorageObservability::redactSecretValue` redacts keys containing "password", "dsn", "secret", or "key" before startup logging.
- Request hardening defaults (CLI-configurable): `--max-request-body-bytes` (65536), `--max-request-target-length` (2048), `--request-id-max-length` (64).
- TLS defaults: minimum TLS 1.2, strong ciphers/curves, `session_tickets = false` by default; mTLS via `client_auth_mode` (`none`/`optional`/`required`); context reloads on `SIGHUP` without restart.

## Delegating to specialized agents

This repo defines specialized subagents in `.claude/agents/` for a design → implement → test → review → docs workflow. Prefer delegating rather than doing everything inline:

- **app-architect** — design decisions, ADRs, interface contracts (does not write code).
- **cpp-expert** — all implementation in `src/`, `include/`, `tests/`.
- **testing-expert** — test generation and coverage analysis; **test-documenter** — Boost.Test Scenario/Boundaries doc comments on existing tests.
- **feature-reviewer** + **security-auditor** — review gate before merge (run together via the `/pr-review` skill).
- **subtask-verifier** — checks an implementation against its spec in `docs/specs/` / `docs/agent/implementation-taskmap.md`.
- **docs-writer** (net-new docs) / **docs-updater** (keeping existing docs in sync with code changes).
- **agent-orchestrator** — coordinates the full sequence for multi-step work spanning several of the above.

Useful skills: `/pr-review`, `/test-gap`, `/verify-subtask`, `/adr-write`, `/document-tests`, `/doc-xref`, `/doc-registry`, `/link-check`, `/secret-scan`.
