# Low-latency URL shortener

This repository currently contains a C++17 HTTP/HTTPS service that started as a key-value URI store and now includes initial URL shortener endpoints.

The intended direction is to evolve this service into a **low-latency URL shortener** with predictable redirect performance, robust operational controls, and clearer domain boundaries.

## Prerequisites

The project requires:

- C++17 compiler (GCC 9+, Clang 12+, or Apple Clang)
- CMake 3.25+
- Ninja (recommended generator)
- Boost 1.74+ (with `system`, `program_options`, `unit_test_framework`)
- OpenSSL
- SQLite 3 development headers
- PostgreSQL client development headers (`libpq`)
- Python 3 (required by CMake during configure/build; also used for integration tests)
- Visual Studio 2022 + vcpkg (Windows only)

A few dependencies are fetched and built automatically at configure time via
CMake `FetchContent`, so you do **not** need to install them yourself:

- `yaml-cpp` (used unless a system package is found)
- `hiredis` (used unless a system package is found)
- `SOCI` (SQLite + PostgreSQL backends; always fetched)

The first configure therefore clones and builds these from source and will take
noticeably longer than subsequent builds.

## Build

The build is a standard out-of-source CMake build. After installing the
platform dependencies below, configure and build with:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja
cmake --build .
```

The server binary is produced at `build/url_shortener`.

### Linux (Ubuntu 24.04 / 22.04)

Install dependencies with the helper script:

```bash
./scripts/setup_ubuntu_dependencies.sh
```

Or install them manually:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git pkg-config ca-certificates curl \
    libssl-dev libboost-all-dev libsqlite3-dev libpq-dev python3
```

Then configure and build:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja
cmake --build .
```

### macOS

Three dependency-management strategies are supported. **Homebrew** is the most
native and common macOS developer option. **MacPorts** is suitable if you already
maintain a MacPorts tree. **vcpkg** gives the strongest cross-platform
reproducibility and mirrors the Windows CI workflow exactly.

Choose exactly **one** strategy and avoid mixing packages from different managers:
duplicate Boost or OpenSSL installations will confuse CMake's library discovery.

#### Option A — Homebrew (recommended)

**Install prerequisites**

```bash
brew install cmake ninja boost openssl@3 libpq sqlite python3
```

**Configure and build**

`openssl@3`, `libpq`, and `sqlite` are keg-only on macOS, so CMake will not find
them on the default system search path. Point CMake at all relevant Homebrew
prefixes at once:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix boost);$(brew --prefix openssl@3);$(brew --prefix libpq);$(brew --prefix sqlite)"
cmake --build .
```

**Boost component-config workaround (CMake 4.x)**

Homebrew's Boost installation provides a top-level `BoostConfig.cmake` but may
omit the per-component config files (`boost_systemConfig.cmake`, etc.). With
CMake 4.x the configure step fails with something like:

```
Could not find a package configuration file provided by "boost_system"
...
  /usr/local/lib/cmake/Boost-1.90.0/boost_systemConfig.cmake
```

Add `-DBoost_NO_BOOST_CMAKE=ON` to bypass `BoostConfig.cmake` and fall back to
library-filename–based discovery:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix boost);$(brew --prefix openssl@3);$(brew --prefix libpq);$(brew --prefix sqlite)" -DBoost_NO_BOOST_CMAKE=ON
cmake --build .
```

**Architecture note**

On Apple Silicon the Homebrew prefix is `/opt/homebrew`; on Intel Macs it is
`/usr/local`. `brew --prefix <formula>` resolves the correct path for you in the
terminal and in shell scripts.

**CLion guidance**

CLion's **CMake Profile → CMake options** field does **not** execute shell
substitutions, so entering `$(brew --prefix openssl@3)` passes that literal
string to CMake and fails. Use one of the following approaches instead.

*Concrete paths (simplest).* Run `brew --prefix <formula>` in a terminal to get
the actual path for your machine, then paste it. Common values:

| Formula     | Apple Silicon                 | Intel Mac                  |
|-------------|-------------------------------|----------------------------|
| `boost`     | `/opt/homebrew/opt/boost`     | `/usr/local/opt/boost`     |
| `openssl@3` | `/opt/homebrew/opt/openssl@3` | `/usr/local/opt/openssl@3` |
| `libpq`     | `/opt/homebrew/opt/libpq`     | `/usr/local/opt/libpq`     |
| `sqlite`    | `/opt/homebrew/opt/sqlite`    | `/usr/local/opt/sqlite`    |

Paste as a single **CMake options** value (semicolons, no line breaks). Apple
Silicon example:

```
-DCMAKE_PREFIX_PATH=/opt/homebrew/opt/boost;/opt/homebrew/opt/openssl@3;/opt/homebrew/opt/libpq;/opt/homebrew/opt/sqlite -DBoost_NO_BOOST_CMAKE=ON
```

Replace `/opt/homebrew` with `/usr/local` on an Intel Mac.

*Environment variable (portable).* In CLion's CMake Profile, open the
**Environment** section and add:

```
HOMEBREW_PREFIX=/opt/homebrew
```

Then reference it in the **CMake options** field:

```
-DCMAKE_PREFIX_PATH=$ENV{HOMEBREW_PREFIX}/opt/boost;$ENV{HOMEBREW_PREFIX}/opt/openssl@3;$ENV{HOMEBREW_PREFIX}/opt/libpq;$ENV{HOMEBREW_PREFIX}/opt/sqlite -DBoost_NO_BOOST_CMAKE=ON
```

#### Option B — MacPorts

Suitable if you already manage a MacPorts installation. Adjust the PostgreSQL
version suffix to what MacPorts currently provides:

```bash
sudo port install cmake ninja boost openssl sqlite3 postgresql16 python312
```

MacPorts installs under `/opt/local`. Pass that prefix to CMake:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja -DCMAKE_PREFIX_PATH=/opt/local
cmake --build .
```

If Boost component-config discovery fails (same root cause as the Homebrew case
above), add `-DBoost_NO_BOOST_CMAKE=ON`:

```bash
mkdir cmake-build && cd cmake-build
cmake .. -G Ninja -DCMAKE_PREFIX_PATH=/opt/local -DBoost_NO_BOOST_CMAKE=ON
cmake --build .
```

#### Option C — vcpkg

vcpkg provides the strongest cross-platform reproducibility. The project ships a
`vcpkg.json` manifest at the repository root that declares all required ports
(Boost components, OpenSSL, libpq, sqlite3, yaml-cpp, hiredis), so vcpkg installs
everything automatically during the first CMake configure — no separate
`brew vcpkg install` step is needed.

**Bootstrap vcpkg** (one-time setup):

```bash
git clone https://github.com/microsoft/vcpkg.git ~/.vcpkg
~/.vcpkg/bootstrap-vcpkg.sh
```

**Configure and build**

Choose the triplet for your machine:

##### Apple Silicon

```bash
mkdir cmake-build && cd cmake-build
vcpkg install --triplet arm64-osx
cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE=~/.vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=arm64-osx
cmake --build .
```

##### Intel Mac

```
mkdir cmake-build && cd cmake-build
vcpkg install --triplet x64-osx
cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE=~/.vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-osx
cmake --build .
```

vcpkg downloads and builds all declared ports on the first configure. This can
take several minutes; subsequent builds use the local binary cache and are fast.

> **Important.** When using vcpkg, do **not** also install Boost or OpenSSL from
> Homebrew or MacPorts. Conflicting headers and library search paths will cause
> CMake discovery errors.

### Windows

```powershell
mkdir cmake-build && cd cmake-build
vcpkg install --triplet x64-windows
cmake .. -G "Visual Studio 17 2022" -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --target url_shortener
```

## Run

### Show all options

```bash
./cmake-build/url_shortener --help
```

### HTTP only

```bash
./cmake-build/url_shortener
```

### HTTP only on a custom port (positional shorthand)

```bash
./cmake-build/url_shortener 9090
```

### HTTPS enabled

```bash
./cmake-build/url_shortener \
  --http-port 8080 \
  --tls-enabled true \
  --https-port 8443 \
  --tls-cert /path/to/server.crt \
  --tls-key /path/to/server.key \
  --http-redirect-to-https true \
  --hsts-max-age 300
```

## API examples

### Create short URL

```bash
curl -i -X POST http://localhost:8000/api/v1/short-urls \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://example.com/docs","code":"docs"}'
```

### Resolve short URL

```bash
curl -i http://localhost:8000/r/docs
```

### Get short URL metadata

```bash
curl -i http://localhost:8000/api/v1/short-urls/docs
```

### Patch link management fields

```bash
curl -i -X PATCH http://localhost:8000/api/v1/links/docs \
  -H 'Content-Type: application/json' \
  -d '{"enabled":false,"tags":["campaign","email"],"metadata":{"owner":"growth"}}'
```

### Soft delete / restore

```bash
curl -i -X DELETE http://localhost:8000/api/v1/links/docs
curl -i -X POST http://localhost:8000/api/v1/links/docs/restore
```

### Preview and stats

```bash
curl -i http://localhost:8000/api/v1/links/docs/preview
curl -i http://localhost:8000/api/v1/links/docs/stats
```

## CLI mode

The binary also runs as a one-shot link-management client when invoked as
`url_shortener link <verb>`. No server process is started; the command executes,
writes a JSON object to stdout (or a diagnostic to stderr), and exits.

```bash
# Create a short link - exit 0, JSON on stdout
./cmake-build/url_shortener link create \
  --url https://example.com/docs --slug docs

# Fetch by slug
./cmake-build/url_shortener link get --slug docs

# Soft-delete, then restore
./cmake-build/url_shortener link delete --slug docs
./cmake-build/url_shortener link restore --slug docs
```

**Note.** The CLI uses the in-memory link store, which is per-process and
non-persistent. Two separate invocations do **not** share state; a link created
in one process is invisible to `link get` in a separate process.

Full reference - all nine verbs (`create`, `get`, `update`, `delete`, `enable`,
`disable`, `restore`, `preview`, `stats`), flags, and exit codes:
[`docs/cli/README.md`](docs/cli/README.md).

### Stage 2 migration notes

- Stage 2 management fields are backward-compatible and default to: `enabled=true`, `deleted_at=null`, `tags=[]`, `metadata={}`, and `campaign=null` when absent.
- Redirect stats are **provisional operational counters** in the current in-memory implementation and may reset on process restart/state loss.
- Old clients using existing create/read/redirect flows continue to work; new lifecycle operations live under `/api/v1/links/{slug}/...`.
- Placeholder extension routes for future work are available at `GET /api/v1/links/{slug}/qr` and `GET /api/v1/links/{slug}/routing`, both currently returning `501 feature_not_enabled`.
- The C++ `RouteRegistry` is the source of truth for the API inventory. A
  generated Markdown reference is maintained at [`docs/api/README.md`](docs/api/README.md).

## Current state

### What exists today

- Single binary server (`url_shortener`) built with Boost.Asio/Beast + OpenSSL.
- HTTP listener and optional HTTPS listener.
- Optional HTTP -> HTTPS redirect (`308`).
- TLS hardening controls (TLS version/ciphers/curves/session config), optional mTLS, and SIGHUP TLS context reload.
- Shortener API:
  - Canonical link management under `/api/v1/links`.
  - Compatibility create/read aliases under `/api/v1/short-urls`.
  - Redirects via `GET /{slug}` and compatibility `GET /r/{slug}`.
  - URI-store fallback routes for legacy arbitrary paths.
- Router-backed dispatch with route metadata in `RouteRegistry`.
- API reference generated from route metadata in `docs/api/README.md`.

### Current limitations

- No latency-focused benchmark + profiling loop committed as part of CI.
- Minimal JSON parsing/validation strategy (string scanning) suitable for early stage but not ideal for long-term correctness/performance.
- Production observability is still intentionally lightweight.

That means:

* **POST** for link creation
* **GET** for reads, preview, stats, and redirect
* **PATCH** for management updates
* **DELETE** for soft delete
* optional **POST** for action-style endpoints like `/enable`, `/disable`, `/restore`

### Why not switch away from the current stack now

Your actual bottlenecks are elsewhere:

* redirect fast path is not yet optimized/measured

Those are much more important than whether the API is “pure REST” or whether you adopt another RPC style. The architecture doc explicitly calls out storage abstraction, robust serialization, redirect fast path, and metrics as the next gaps to close. 

### Why POST is the right choice for URL creation

For a shortener, the payload is literally a URL, and that matters.

Using **GET with query parameters** for creation would be a bad fit because:

* long URLs can exceed practical URL length limits
* URLs in query strings are noisier in logs and proxies
* escaping/encoding becomes ugly fast
* creation is not safe/idempotent in the HTTP sense

Stage 1 already specifies `POST /api/v1/links` with JSON body containing `url`, optional `slug`, `expires_at`, and `redirect_type`. That is the right contract. 

### Should it be “REST API”?

Yes, but in a **pragmatic** sense, not a dogmatic one.

A good shape for this project is:

* `POST /api/v1/links` — create
* `GET /api/v1/links/{slug}` — read by slug
* `GET /api/v1/links/id/{id}` — read by id
* `PATCH /api/v1/links/{slug}` — update management fields
* `DELETE /api/v1/links/{slug}` — soft delete
* `GET /api/v1/links/{slug}/preview` — preview
* `GET /api/v1/links/{slug}/stats` — stats
* `GET /{slug}` or `GET /r/{slug}` — redirect

### Proceed Recommendation

Do **not** adopt a new transport technology now.

Do **migrate the API shape** gradually toward the staged resource model:

1. Keep current Beast/Asio/OpenSSL runtime.
2. Introduce the `Link` domain model and repository first.
3. Add `/api/v1/links` as the canonical API.
4. Keep existing `/api/v1/short-urls` as a compatibility alias for now.
5. Keep `/r/{code}` during transition; later decide whether public redirect should be `/{slug}`.
6. Only after storage abstraction and performance baselines are in place should you reconsider protocol choices.


## Intended target: low-latency URL shortener

The development target is a service optimized for fast redirects and safe operations under load.

### Functional target

- Canonical short-link resource model (`id`, `slug`, `target_url`, lifecycle/status fields).
- Stable versioned API for create/read/update/delete + lifecycle controls.
- Deterministic redirect behavior for active/disabled/expired/deleted states.
- Backward compatibility strategy for legacy endpoints (or explicit deprecation path).

### Performance target

- Keep redirect path extremely short (lookup + response, minimal allocations).
- Establish explicit p50/p95/p99 latency budgets for redirect and create operations.
- Add repeatable micro-benchmarks and load tests for regression detection.
- Support horizontal scaling through clean storage abstraction and external datastore integration.

### Operational target

- Better observability (structured logs, metrics, and health endpoints).
- Safe configuration + startup validation.
- Strong TLS defaults preserved for HTTPS deployments.
- Clear rollback/migration strategy as data model evolves.

## Stage 1 core API (canonical)

### Config

- `SHORTENER_BASE_DOMAIN` (required, must be absolute `http://` or `https://` domain without path/query/fragment)
- `SHORTENER_DEFAULT_REDIRECT_TYPE` (`temporary` default)
- `SHORTENER_DEFAULT_EXPIRY_SECONDS` (optional)
- `SHORTENER_GENERATED_SLUG_LENGTH` (`7` default)
- `SHORTENER_ALLOW_PRIVATE_TARGETS` (`false` default)

### Endpoints

- `POST /api/v1/links`
- `GET /api/v1/links/id/{id}`
- `GET /api/v1/links/{slug}`
- `GET /{slug}` (canonical redirect)
- `GET /r/{slug}` (compatibility alias)

### Redirect behavior

| State | Response |
|---|---|
| Missing | `404 not_found` |
| Disabled | `410 link_disabled` |
| Expired | `410 link_expired` |
| Active temporary | `302 Found` |
| Active permanent | `301 Moved Permanently` |

### Curl flow examples

```bash
curl -i -X POST http://localhost:8000/api/v1/links   -H 'Content-Type: application/json'   -d '{"url":"https://example.com/docs"}'
```

```bash
curl -i -X POST http://localhost:8000/api/v1/links   -H 'Content-Type: application/json'   -d '{"url":"https://example.com/docs","slug":"docs"}'
```

```bash
curl -i http://localhost:8000/api/v1/links/docs
curl -i http://localhost:8000/api/v1/links/id/<id>
```

```bash
curl -i http://localhost:8000/docs
```

### Redirect benchmark baseline (Stage 1)

Use `scripts/benchmark_redirect.py` to capture baseline redirect throughput and latency.

```bash
scripts/benchmark_redirect.py --base-url http://127.0.0.1:28080 --concurrency 32 --duration 10
```

Sample baseline on local dev VM (8 vCPU, Ubuntu 24.04, Mar 25, 2026):

- hot-path redirect (`302`): ~11k req/s, p50 2.1 ms, p95 8.8 ms, p99 15.4 ms
- mixed outcomes (hit/miss/expired/disabled): ~9k req/s aggregate, p99 < 20 ms

The redirect path is treated as a protected fast path. Management-plane changes under `/api/v1/...` should not add extra logic to redirect resolution.

## Stage 4 (scope 5.1) analytics notes

Implemented in this stage slice:
- Internal `ClickEvent` capture model on redirect attempts.
- Bounded in-memory analytics queue with non-blocking drop-on-overflow behavior.
- Redirect flow instrumentation is best-effort and does not change redirect response behavior.

Current config knobs:
- `--analytics-enabled` (default: `true`)
- `--analytics-queue-capacity` (default: `1024`)
- `--analytics-client-hash-salt` (default dev value; set explicitly in non-dev environments)

Privacy defaults in current scope:
- Raw client identifiers are not persisted; a salted HMAC-SHA256-derived `client_id_hash` is captured.
- `Referer` and `User-Agent` are length-bounded before enqueue to keep memory usage predictable.

Deferred to later Stage 4 scope:
- Worker persistence pipeline
- Aggregate analytics read API
- Retention hooks

## Stage 5 (scope 1) observability + hardening notes

Implemented in this stage slice:
- Request ID propagation (`X-Request-Id`) with validation/fallback generation.
- Stable error envelope request correlation (`error.request_id`).
- Request completion structured logs with method/path/route/status/latency fields.
- In-process counters exposed via `GET /metrics`.
- Liveness/readiness probes: `GET /healthz`, `GET /readyz`.
- Early request-boundary hardening: max target length and max body size caps.

Current config knobs:
- `--request-id-max-length` (default: `64`)
- `--max-request-body-bytes` (default: `65536`)
- `--max-request-target-length` (default: `2048`)

## Tests

```bash
ctest --test-dir cmake-build --output-on-failure
```

(Direct invocation still works: `python3 test/http_client_test/http_client_test.py`.)

## Repo guides

- `ARCHITECTURE.md` — high-level architecture overview; detailed specs in [`docs/adr/`](docs/adr/).
- `docs/adr/` — Architecture Decision Records (system architecture, backend topology, performance contract, security model, SQL persistence).
- `docs/storage/overview.md` - Stage 03 storage abstraction (scope 1) overview.
- `docs/stages/` - staged specification documents.
- `scripts/setup_ubuntu_dependencies.sh` - Ubuntu dependency bootstrap.
