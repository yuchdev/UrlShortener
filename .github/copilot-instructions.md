# Copilot Instructions for UrlShortener

## Build

This project requires vcpkg on Windows. `CMakeLists.txt` fails fast if neither `CMAKE_TOOLCHAIN_FILE` nor `VCPKG_ROOT` is set.

Use the README Windows workflow with a normal out-of-source `cmake-build`
directory:

```powershell
cmake -S . -B cmake-build -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
```

Build the main binary:
```powershell
cmake --build cmake-build --target url_shortener --config Debug
```

Build a specific test target before running it:
```powershell
cmake --build cmake-build --target <target_name> --config Debug
```

On Linux/macOS (generic):
```bash
cmake -B build -G Ninja
cmake --build build
```

Auto-fetched dependencies (do not install manually): `yaml-cpp`, `hiredis`, `SOCI` (SQLite + PostgreSQL backends).

## Testing

Tests are registered via `add_test` in `CMakeLists.txt` and grouped by CTest labels: `unit`, `contract` (backend-agnostic "mock"/fake suites), `integration`, and `e2e` (shell-based scenario tests under `tests/e2e/scripts/`, registered only on non-Windows hosts with a `bash` interpreter — toggle with `-DURLSHORTENER_ENABLE_E2E_TESTS=OFF`).

**Always use `--test-dir` pointing to the existing build directory.**

Run a single test by exact name:
```powershell
ctest --test-dir cmake-build -C Debug -R "^<test_name>$" --output-on-failure
```

Run by label:
```powershell
ctest --test-dir cmake-build -C Debug -L unit --output-on-failure
```

Run **every category** (unit + mock/contract + integration + e2e) in a single command via a label union regex:
```powershell
ctest --test-dir cmake-build -C Debug -L "unit|contract|integration|e2e" --output-on-failure
```

Run all tests (equivalent, no label filter):
```powershell
ctest --test-dir cmake-build -C Debug --output-on-failure
```

> Note: `e2e` tests require a POSIX shell, `python3`, and Linux runtime facilities (`/proc`, `os.kill`); they register and pass on the Ubuntu CI runner, not on native Windows. The `e2e` CTest entries invoke the same `tests/e2e/scripts/run_section.sh` used by the `e2e-tests.yml` pipeline.

### Test target naming

Test target name depends on which CMake source list the test belongs to:

- `CORE_BASIC_UNIT_SOURCES` and `LINK_MANAGEMENT_UNIT_SOURCES` → target name = **filename stem** (e.g., `01_url_validation_accept_test`)
- All other suites registered via `register_labeled_cpp_tests(...)` → target name = **`<parent_dir>__<filename_stem>`** (e.g., `inmemory__01_create_and_get_roundtrip`)

### Adding new tests

1. Add `*.cpp` to `tests/unit/`, `tests/integration/`, or `tests/contract/` in the appropriate subdirectory.
2. Append the path to the matching source list in `CMakeLists.txt`.
3. Use numeric prefixes for test filenames (`01_...`, `02_...`) consistent with sibling tests.
4. Build the single target, then run via exact-name `ctest -R "^...$"`.

## Architecture overview

Single-binary async HTTP/HTTPS server (Boost.Asio + Boost.Beast + OpenSSL).

**Runtime data flow:**
1. `src/main.cpp` parses CLI flags into `ServerConfig`, initializes `io_context`, and signals (`SIGINT`/`SIGTERM`/`SIGHUP`).
2. `HttpServer` (`src/http/`) accepts connections and spawns `PlainSession` or `TlsSession` per connection.
3. `handleShortenerRequest` in `src/http/request_handlers.cpp` delegates to the
   application `Router`; `RouterBuilder` registers route-specific handlers.
4. `LinkService` (`src/core/`) coordinates redirect resolution through `IMetadataRepository`, `ICacheStore`, and `IAnalyticsSink`.
5. `BoundedClickEventQueue` receives analytics events non-blocking (drop-on-overflow); `AnalyticsWorker` drains the queue to `IClickEventRepository` in the background.
6. `StorageFactory` (`src/composition/`) constructs all storage adapters from `StorageConfig` at startup.

**All `.cpp` files compiled into `url_shortener_common` (static lib) must be listed in `sources.cmake`. New source files must be added there.**

---

## Module reference

### `core/` — Domain types and services

**`include/url_shortener/core/types.h`** — canonical domain types:
- `Link` — full link entity with `id`, `slug`, `target_url`, `enabled`, `tags`, `metadata`, `campaign`, `stats`, `redirect_type`, soft-delete timestamp.
- `ClickEvent` — analytics event tied to a redirect attempt.
- `RedirectType` — `temporary` (302) / `permanent` (301).
- `LinkStatus` — `active` / `disabled` / `expired` / `deleted`.
- `RepoError` (legacy) — used by older storage paths; newer code uses `include/url_shortener/storage/errors/RepoError.hpp`.

**`include/url_shortener/core/LinkService.hpp`** — primary domain service:
- `LinkService::Resolve(slug)` → `ResolveResult{status, target_url, cache_hit}` — the redirect fast path. Implement new redirect behavior here, not in request handlers.
- `LinkService::Update` / `Delete` — write through cache invalidation.
- `ResolveStatus`: `redirect`, `not_found`, `inactive`, `expired`.

**`include/url_shortener/core/clock.hpp`** — clock abstraction for deterministic testing:
- `SystemClock` — production wall clock.
- `ManualClock` — injectable test clock with `set()` and `advance()`. Pass to `LinkService` and storage adapters in tests instead of `SystemClock`.

**`include/url_shortener/core/utils.h`** — utilities called across modules:
- URL validation: `isValidHttpUrl`, `isValidSlug`, `isReservedSlug`, `normalizeTargetUrl`, `isPrivateHost`.
- JSON helpers: `extractJsonStringField`, `extractJsonBoolField`, `extractJsonStringArrayField`, `extractJsonFlatObjectStringField` — all operate on raw string bodies without a JSON library. Used throughout `request_handlers.cpp`.
- ID/slug generation: `generateSlug(length)`, `generateId()` (UUID-like).
- Crypto: `hashClientIdentifier(raw, salt)` — HMAC-SHA256 for analytics client IDs.
- Link helpers: `resolveLinkStatus(link)`, `redirectStatusFor(link)`.

---

### `http/` — Server and request handling

**`HttpServer`** manages HTTP and HTTPS acceptors, `run()`/`stop()` lifecycle, and `reloadTlsContext()` (triggered by `SIGHUP`).

**`handleShortenerRequest`** is the single entry point for HTTP routing and
delegates to the application `Router`. `RouterBuilder` registers handlers from
`src/http/handlers/`, and `RouteRegistry` is the source of truth for route
metadata, stable labels, operation IDs, compatibility aliases, placeholders, and
generated API docs (`docs/api/README.md`).

**`RequestContext`** carries `request_id`, `route_label`, and `started_at` through the request lifecycle. `X-Request-Id` is accepted from callers (validated/length-bounded) or generated if absent.

**`MetricsRegistry`** holds atomic counters for inflight requests, parse errors, malformed requests, TLS reload counts, and a per-route/status map. Exposed via `GET /metrics` through `renderMetrics()`.

**`RedirectAnalyticsHook`** (`src/http/`) bridges the HTTP layer to `AnalyticsService::RecordRedirectAttempt`. Called after every redirect outcome — pass `RedirectAnalyticsInput` with slug, host, status code, referrer, user-agent, and client network identifier.

**`AnalyticsStatsHandler`** — handles `GET /api/v1/links/{slug}/stats`. Queries `IClickEventRepository` with time range and bucket parameters.

**Request hardening defaults** (configurable via CLI):
- `--max-request-body-bytes` (default: 65536)
- `--max-request-target-length` (default: 2048)
- `--request-id-max-length` (default: 64)

---

### `storage/` — Storage interfaces and backends

#### Interfaces (ports)

All storage adapters implement these interfaces. Depend on interfaces, not concrete types.

| Interface | Location | Responsibility |
|---|---|---|
| `IMetadataRepository` | `storage/IMetadataRepository.hpp` | Link CRUD: `CreateLink`, `GetByShortCode`, `UpdateLink`, `DeleteLink`, `ListLinks`, `Exists` |
| `ICacheStore` | `storage/ICacheStore.hpp` | Cache-aside: `Get`, `Set` (with optional TTL), `Delete`, `ClearByPrefix` |
| `IRateLimiter` | `storage/IRateLimiter.hpp` | `Allow(key, limit, window)` → `RateLimitDecision` |
| `IAnalyticsSink` | `storage/IAnalyticsSink.hpp` | `Emit(event)`, `Flush()` — forwarding sink called in redirect hot path |
| `IClickEventRepository` | `analytics/IClickEventRepository.hpp` | Persistence + aggregate reads for analytics worker |

#### Error types

| Type | Values |
|---|---|
| `RepoError` | `none`, `not_found`, `already_exists`, `invalid_argument`, `transient_failure`, `permanent_failure` |
| `CacheError` | See `storage/errors/CacheError.hpp` |
| `RateLimitError` | See `storage/errors/RateLimitError.hpp` |

All methods accept an optional `T* error` out-parameter. Never throw for expected errors — write to the out-param and return `false`/`std::nullopt`.

#### Storage models

| Model | Purpose |
|---|---|
| `LinkRecord` | Persisted short-link row: `id`, `short_code`, `target_url`, timestamps, `is_active`, `version`, `attributes` map |
| `CreateLinkRequest` | Input for creates; `isValid()` enforces `short_code` regex `^[A-Za-z0-9_-]{3,64}$` and non-empty `target_url` |
| `CacheValue` | Cached representation of a resolved link |
| `ListLinksQuery` | Filter/pagination parameters for `ListLinks` |
| `RateLimitDecision` | Result of rate-limit evaluation |
| `LinkAccessEvent` | Event model for access tracking |

#### Backends

**In-memory** (`storage/inmemory/`): `InMemoryMetadataRepository`, `InMemoryCacheStore`, `InMemoryAnalyticsSink`, `InMemoryRateLimiter`. Primary/secondary index maps protected by `std::shared_mutex`. Default backend; no external dependencies.

**SQL** (`storage/sql/`): Backend-neutral layer shared by SQLite and PostgreSQL.
- `SqlMetadataRepository` — implements `IMetadataRepository` via `ISqlSessionFactory` + `SqlDialect` + `SqlMetadataRowMapper`.
- `SqlDialect` — abstract interface for backend SQL statements (`BootstrapSchemaSql`, `InsertSql`, etc.). Implementations: `SqliteSqlDialect`, `PostgresSqlDialect`.
- `ISqlSession` / `ISqlSessionFactory` — session abstraction over SOCI. Row model: `SqlRow`.
- `SqlMetadataRepository` bootstraps schema lazily on first use (`EnsureBootstrapped`).

**SQLite** (`storage/sqlite/`): `SqliteSessionFactory`, `SqliteErrorMapper`, `SqliteSqlDialect`. Uses SOCI SQLite3 backend.

**PostgreSQL** (`storage/postgres/`): `PostgresSessionFactory`, `PostgresErrorMapper`, `PostgresSqlDialect`, `PostgresMigrationRunner`. Uses SOCI PostgreSQL backend. Numbered migration scripts in `db/migrations/postgres/`.

**Redis** (`storage/redis/`): `RedisCacheStore` (implements `ICacheStore`), `RedisRateLimiter` (implements `IRateLimiter`). Uses `hiredis`. Non-authoritative — fail-open on errors.

#### Cache-aside contract

1. `ICacheStore::Get` first; on miss/failure → `IMetadataRepository::GetByShortCode`.
2. Populate cache best-effort after a repository read.
3. After `UpdateLink` or `DeleteLink` → invalidate cache via `ICacheStore::Delete`.
4. Cache failures **never** fail the request (fail-open).

#### Adding a new backend

See `docs/storage/how-to-add-adapter.md`. Required steps: implement interfaces → map errors → register in config/factory → add contract tests → add backend unit + integration tests → update CI.

---

### `analytics/` — Click event pipeline

**Pipeline:** `RedirectAnalyticsHook` → `AnalyticsService::RecordRedirectAttempt` → `BoundedClickEventQueue::TryEnqueue` → `AnalyticsWorker` (background thread) → `IClickEventRepository`.

- `BoundedClickEventQueue` — thread-safe, capacity-bounded, drops on overflow (`EnqueueResult::dropped`). Call `NotifyStop()` during shutdown so `WaitDequeue` unblocks promptly.
- `AnalyticsWorker` — background thread that batches events from the queue and calls `IClickEventRepository` with retry. States: `Stopped` / `Running` / `Stopping`. Call `Stop()` before destroying.
- `AnalyticsService` — stateless facade. Builds `ClickEvent` via `ClickEventBuilder`, sanitizes (length-bounded fields), and enqueues. `analytics_enabled = false` → no-op, no worker required.
- `ClickEventSanitizer` — enforces byte limits: `max_domain_bytes` (255), `max_referrer_bytes` (512), `max_user_agent_bytes` (512).
- `ClientIdHasher` — produces `client_id_hash` via HMAC-SHA256 with configured salt. Raw client identifiers are never stored.
- `RetentionCleanupJob` — scheduled job that deletes events older than `retention_days`.
- `AnalyticsMetrics` — counter interface for queue stats (enqueued, dropped, flushed).

**`AnalyticsConfig`** fields: `enabled`, `queue_capacity` (8192), `batch_size` (128), `flush_interval` (1000ms), `retry.*`, `retention.*`, `sanitization.*`, `client_hash_salt`. Load from YAML — see `config/analytics.example.yaml`.

Analytics failures **never** affect redirect responses. The entire pipeline is best-effort.

---

### `observability/` — Logging and metrics

**`Logger` / `LoggerFactory`** — structured logger. Use `LogFields` to build a `std::map<std::string, std::string>` and pass to `logStructured(level, msg, fields)`.

**`StorageObservability`** — helper for building stable metric label maps (`repositoryMetricLabels`, `cacheMetricLabels`) and redacting secrets before startup logging (`redactSecretValue`). Keys containing "password", "dsn", "secret", or "key" are redacted.

**`LoggingConfig` / `LogLevel`** — configure log verbosity.

**Operational endpoints** (always available):
- `GET /healthz` — liveness probe.
- `GET /readyz` — readiness probe.
- `GET /metrics` — in-process counters (via `renderMetrics()`).

Request-completion logs emit method, path, route label, status, and latency for every handled request.

---

### `security/` — Authentication and access control

**Auth is local-only.** The auth broker must never be exposed to the public internet. External apps connect via Unix domain socket (`/run/urlshortener/auth.sock`) or loopback HTTP — never `0.0.0.0`.

**`ControlSet`** — two roles:
- `Admin` — read + write + migrate + manage users.
- `User` — read only.

**`AccessGuard`** — obtained from an authenticated session. Call `requireRead()`, `requireWrite()`, `requireMigration()`, or `requireUserManagement()`. Throws `AccessDeniedError` if permission is absent.

**`AuthBrokerService`** — core auth operations:
- `createSession(username, plainPassword)` → `CreateSessionResult{ok, rawToken, session, user}`. `rawToken` must be returned to the caller and never stored/logged.
- `introspectToken(rawToken)` → `IntrospectResult{active, user, session}`.
- `revokeToken(rawToken)`.
- `guardFor(user)` → `AccessGuard` for in-process use.

**`FirstAdminBootstrap`** — one-time creation of the first admin user. Throws `AdminAlreadyExistsError` if an active admin already exists.

**`UserManagementService`** — user CRUD guarded by admin `ControlSet`.

**Security invariants:**
1. Passwords stored as PBKDF2-SHA256 hash (100k iterations, 16-byte salt) — never plaintext.
2. Raw tokens never stored — only SHA-256 hash persisted.
3. Failed login attempts return generic errors — do not reveal whether username or password was wrong.
4. All auth operations recorded in `auth_audit_log`. Log never stores passwords, hashes, raw tokens, or client secrets.

**Database tables** (see `db/migrations/postgres/002_add_users_auth_and_access_control.up.sql`): `app_users`, `auth_sessions`, `service_clients`, `auth_audit_log`.

---

### `config/` — Storage configuration

**`StorageConfig`** — four independent backend sections:

```cpp
struct StorageConfig {
    MetadataBackendConfig metadata;   // backend: "inmemory" | "sqlite" | "postgres"
    CacheBackendConfig    cache;      // backend: "none" | "redis"
    AnalyticsBackendConfig analytics; // backend: "noop" | "inmemory" | "sqlite" | "postgres"
    RateLimitBackendConfig rate_limit; // backend: "inmemory" | "redis", enabled flag
};
```

Parse from YAML: `ParseStorageConfigFile(path)` or `ParseStorageConfigYaml(yaml_string)`. Throws `std::runtime_error` on invalid config. Test config parsing: `tests/unit/config/` and `tests/integration/config/`.

**`ServerConfig`** (in `core/config.h`) — top-level runtime config from CLI flags. Key fields: `http_port`, `tls.*`, `shortener_base_domain`, `shortener_generated_slug_length`, `analytics_enabled`, `analytics_client_hash_salt`, request hardening limits.

---

### `composition/` — Wiring

**`BuildStorageAdapters(config, clock)`** — constructs all four storage adapters (`metadata`, `cache`, `analytics`, `rate_limiter`) and returns them as `StorageAdapters`. This is the single point where backend selection happens. Pass `SystemClock` in production; pass `ManualClock` in tests.

---

## Key conventions

**Naming (enforced by `.clang-tidy`):**
- Everything `lower_case` except template parameters (`CamelCase`) and macros (`UPPER_CASE`).
- Private/protected members: `lower_case` with `m_` prefix (e.g., `m_mutex_`, `m_bootstrapped_`). Struct fields and public members use plain `lower_case`.

**Formatting (enforced by `.clang-format`):**
- 4-space indent, 80-column limit.
- `BreakBeforeBraces: Custom` — braces on new line for class/function/enum/struct definitions.
- Includes regrouped: std library first, then system, then local.

**Error handling:**
- Repository/cache/rate-limit methods return `bool` or `std::optional<T>` and write to an optional error out-parameter (`RepoError*`, `CacheError*`, etc.).
- Do not throw for expected errors (not_found, already_exists, etc.). Throw only for programming errors or unrecoverable startup failures.
- `AccessDeniedError` and `AdminAlreadyExistsError` are the only expected exceptions from security layer.

**C++ standard:** C++17 only. Do not use C++20 features.

**Redirect fast path is protected:** Logic under `GET /r/{slug}` and `GET /{slug}` must stay minimal — cache lookup + repository fallback + redirect response. Do not add management-plane logic to this path.

**Analytics is best-effort:** Queue overflow is silent. Analytics failures must never affect redirect response behavior or latency.

**JSON parsing:** Uses manual string scanning helpers from `core/utils.h` (`extractJsonStringField` etc.) — no JSON library. These are the approved parsing utilities; do not add a JSON dependency.

**API shape:** Canonical resource is `/api/v1/links/{slug}`. Endpoints `/api/v1/short-urls/...` and `/r/{slug}` are compatibility aliases. New work targets the `/api/v1/links/` prefix.

**Config knobs:** CLI flags parsed in `src/cli_parser.cpp`. Storage backend selection via YAML (`ParseStorageConfigFile`). Key environment variables: `SHORTENER_BASE_DOMAIN`, `SHORTENER_DEFAULT_REDIRECT_TYPE`, `SHORTENER_GENERATED_SLUG_LENGTH`, `SHORTENER_ALLOW_PRIVATE_TARGETS`, `URL_SHORTENER_ANALYTICS_HASH_SALT`.

**TLS defaults:** Minimum TLS 1.2, strong cipher suites and curves are set in `TlsConfig`. `session_tickets = false` by default. mTLS is configurable (`client_auth_mode`: `none`/`optional`/`required`). TLS context reloads on `SIGHUP` without restart.

## Test utilities

**`ManualClock`** — inject into `LinkService` and storage adapters to control time in tests:
```cpp
ManualClock clock{std::chrono::system_clock::now()};
clock.advance(std::chrono::seconds(3600)); // advance time
```

**`tools/sqlite_state_assert.py`** — Python helper for integration tests that assert SQLite database state. Used by `tests/unit/test_sqlite_state_assert_*.py`.

**Contract tests** (`tests/contract/metadata/`) — backend-agnostic correctness suite. Run against any `IMetadataRepository` implementation. SQLite and PostgreSQL run these via `tests/integration/sqlite/03_contract_suite_runner.cpp` and `tests/integration/psql/05_contract_suite_runner.cpp`.

**E2E scripts** (`tests/e2e/scripts/sections/`) — shell-based end-to-end scenarios: server boot, URL creation, redirects, expiration, fingerprinting, authentication, permissions, admin API, error handling, concurrency. Run with `tests/e2e/scripts/run_all_sections.sh` against a live server.

## Database migrations

PostgreSQL numbered migrations in `db/migrations/postgres/`:
- `001_create_links` — short links table.
- `002_add_users_auth_and_access_control` — auth tables (`app_users`, `auth_sessions`, `service_clients`, `auth_audit_log`).
- `004_analytics_events` — analytics events table.

SQLite schema bootstrapped lazily by `SqlMetadataRepository::EnsureBootstrapped` on first use (no migration runner for SQLite).
