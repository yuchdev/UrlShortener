# Low-latency URL shortener

A **low-latency URL shortener** with predictable redirect performance, robust operational
controls, and clean domain boundaries between the redirect fast path and the management
plane.

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

The server binary is produced at `cmake-build/url_shortener`.

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
library-filename-based discovery:

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
`vcpkg install` step is needed.

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

```bash
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

## Features

- Single binary server (`url_shortener`) built with Boost.Asio/Beast + OpenSSL.
- HTTP listener and optional HTTPS listener.
- Optional HTTP → HTTPS redirect (`308`).
- TLS hardening controls (minimum TLS 1.2, strong ciphers/curves/session config),
  optional mTLS (`none`/`optional`/`required`), and `SIGHUP` TLS context reload
  without restart.
- Shortener API:
  - Canonical link management under `/api/v1/links`.
  - Compatibility create/read aliases under `/api/v1/short-urls`.
  - Redirects via `GET /{slug}` and compatibility `GET /r/{slug}`.
  - URI-store fallback routes for legacy arbitrary paths.
- Lifecycle controls: enable, disable, soft-delete, restore.
- Click analytics with privacy-preserving client hashing (HMAC-SHA256).
- Liveness/readiness probes and in-process metrics.
- Request hardening: configurable body size and path length caps, request ID
  propagation.
- Router-backed dispatch with route metadata in `RouteRegistry`.
- API reference generated from route metadata: [`docs/api/README.md`](docs/api/README.md).

## Storage backends

The storage layer uses a ports-and-adapters design (`IMetadataRepository`,
`ICacheStore`, `IRateLimiter`, `IClickEventRepository`) with interchangeable
backend implementations, each exercised by a shared contract test suite:

| Backend    | Role                             | Default |
|------------|----------------------------------|---------|
| In-memory  | metadata, cache, rate limiter    | yes     |
| SQLite     | metadata, analytics events       | no      |
| PostgreSQL | metadata, analytics events       | no      |
| Redis      | cache, rate limiter              | no      |

The `url_shortener` binary currently starts against the in-memory backend, which
requires no external dependencies. YAML-driven backend selection
(`StorageConfig`, `StorageFactory`/`BuildStorageAdapters`) exists in the storage
composition layer and is exercised by the SQLite/PostgreSQL/Redis integration and
contract test suites — see [`docs/storage/configuration.md`](docs/storage/configuration.md)
for the config schema.

Backend tradeoffs are documented in
[`docs/backend-selection.md`](docs/backend-selection.md). For adding a new adapter,
see [`docs/storage/how-to-add-adapter.md`](docs/storage/how-to-add-adapter.md).

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

### API notes

- Management fields default to `enabled=true`, `deleted_at=null`, `tags=[]`,
  `metadata={}`, and `campaign=null` when absent.
- Old clients using existing create/read/redirect flows continue to work; lifecycle
  operations live under `/api/v1/links/{slug}/...`.
- Placeholder extension routes `GET /api/v1/links/{slug}/qr` and
  `GET /api/v1/links/{slug}/routing` return `501 feature_not_enabled`.
- The C++ `RouteRegistry` is the source of truth for the API inventory. A generated
  Markdown reference is maintained at [`docs/api/README.md`](docs/api/README.md).

## CLI mode

The binary also runs as a one-shot link-management client when invoked as
`url_shortener link <verb>`. No server process is started; the command executes,
writes a JSON object to stdout (or a diagnostic to stderr), and exits.

```bash
# Create a short link — exit 0, JSON on stdout
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

Full reference — all nine verbs (`create`, `get`, `update`, `delete`, `enable`,
`disable`, `restore`, `preview`, `stats`), flags, and exit codes:
[`docs/cli/README.md`](docs/cli/README.md).

## Core API

### Configuration

| Variable                           | Default                  | Notes                                         |
|------------------------------------|--------------------------|-----------------------------------------------|
| `SHORTENER_BASE_DOMAIN`            | `http://localhost:8000`  | Must be absolute `http://` or `https://` URL. |
| `SHORTENER_DEFAULT_REDIRECT_TYPE`  | `temporary`              | `temporary` or `permanent`.                   |
| `SHORTENER_DEFAULT_EXPIRY_SECONDS` | (none)                   | Optional default expiry for new links.        |
| `SHORTENER_GENERATED_SLUG_LENGTH`  | `7`                      | Length of auto-generated slugs.               |
| `SHORTENER_ALLOW_PRIVATE_TARGETS`  | `false`                  | Allow private/intranet destination URLs.      |

### Endpoints

| Method   | Path                            | Notes                          |
|----------|---------------------------------|--------------------------------|
| `POST`   | `/api/v1/links`                 | Create a link.                 |
| `GET`    | `/api/v1/links/{slug}`          | Fetch link metadata by slug.   |
| `GET`    | `/api/v1/links/id/{id}`         | Fetch link metadata by ID.     |
| `PATCH`  | `/api/v1/links/{slug}`          | Update mutable fields.         |
| `DELETE` | `/api/v1/links/{slug}`          | Soft-delete a link.            |
| `GET`    | `/api/v1/links/{slug}/preview`  | Preview resolved status.       |
| `GET`    | `/api/v1/links/{slug}/stats`    | Aggregate click statistics.    |
| `POST`   | `/api/v1/links/{slug}/enable`   | Enable a disabled link.        |
| `POST`   | `/api/v1/links/{slug}/disable`  | Disable a link.                |
| `POST`   | `/api/v1/links/{slug}/restore`  | Restore a soft-deleted link.   |
| `GET`    | `/{slug}`                       | Canonical redirect.            |
| `GET`    | `/r/{slug}`                     | Compatibility redirect alias.  |

Compatibility aliases (`/api/v1/short-urls/...`) are also registered; see the full
route reference at [`docs/api/README.md`](docs/api/README.md).

### Redirect behavior

| State            | Response                    |
|------------------|-----------------------------|
| Missing          | `404 not_found`             |
| Disabled         | `410 link_disabled`         |
| Expired          | `410 link_expired`          |
| Active temporary | `302 Found`                 |
| Active permanent | `301 Moved Permanently`     |

### Curl flow examples

```bash
# Create with auto-generated slug
curl -i -X POST http://localhost:8000/api/v1/links \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://example.com/docs"}'
```

```bash
# Create with explicit slug
curl -i -X POST http://localhost:8000/api/v1/links \
  -H 'Content-Type: application/json' \
  -d '{"url":"https://example.com/docs","slug":"docs"}'
```

```bash
# Fetch by slug or ID
curl -i http://localhost:8000/api/v1/links/docs
curl -i http://localhost:8000/api/v1/links/id/<id>
```

```bash
# Redirect
curl -i http://localhost:8000/docs
```

### Redirect benchmark

Use `scripts/benchmark_redirect.py` to measure redirect throughput and latency on
your deployment:

```bash
scripts/benchmark_redirect.py --base-url http://127.0.0.1:8000 --concurrency 32 --duration 10
```

The redirect path (`GET /{slug}`) is a protected fast path: cache lookup,
repository fallback on miss, redirect response. Management-plane changes under
`/api/v1/...` do not add extra logic to redirect resolution.

## Analytics

Click events are captured on every redirect attempt through an in-memory queue
that is non-blocking and drop-on-overflow. Analytics failures never affect redirect
response behavior or latency.

### Configuration

| Flag                          | Default               | Notes                                             |
|-------------------------------|-----------------------|---------------------------------------------------|
| `--analytics-enabled`         | `true`                | Enable or disable click analytics collection.     |
| `--analytics-queue-capacity`  | `1024`                | In-memory event queue capacity.                   |
| `--analytics-client-hash-salt`| (dev default)         | HMAC salt for client ID hashing. Set explicitly   |
|                               |                       | in non-development environments.                  |

### Privacy defaults

- Raw client identifiers are never persisted. An HMAC-SHA256-derived
  `client_id_hash` is captured instead.
- `Referer` and `User-Agent` are length-bounded before enqueue.

For analytics output interpretation, see [`docs/analytics.md`](docs/analytics.md).

## Observability and request hardening

### Observability

- Request ID propagation via `X-Request-Id` header with validation and fallback
  generation.
- Stable error envelope with `error.request_id` correlation field.
- Request completion structured logs with method, path, route, status, and latency
  fields.
- In-process counters exposed at `GET /metrics`.
- Liveness probe: `GET /healthz`. Readiness probe: `GET /readyz`.

### Request hardening

| Flag                          | Default | Notes                                             |
|-------------------------------|---------|---------------------------------------------------|
| `--request-id-max-length`     | `64`    | Maximum accepted `X-Request-Id` header length.    |
| `--max-request-body-bytes`    | `65536` | Request body size cap; requests exceeding this    |
|                               |         | are rejected early.                               |
| `--max-request-target-length` | `2048`  | Request path/target length cap.                   |

## Tests

Run the full test suite:

```bash
ctest --test-dir cmake-build --output-on-failure
```

Run tests by label:

```bash
# Unit tests only
ctest --test-dir cmake-build -L unit --output-on-failure

# Contract tests (backend-agnostic repository suites)
ctest --test-dir cmake-build -L contract --output-on-failure

# Integration tests
ctest --test-dir cmake-build -L integration --output-on-failure

# All labeled suites
ctest --test-dir cmake-build -L "unit|contract|integration|e2e" --output-on-failure
```

Run a single test by name:

```bash
ctest --test-dir cmake-build -R "^<test_name>$" --output-on-failure
```

On Windows or multi-config generators, add `-C Debug` (or the relevant config) to
every `ctest` and `cmake --build` invocation.

The integration test suite also includes a Python harness under `tests/integration/py/`
and `tools/sqlite_state_assert.py` for asserting SQLite state directly.

## Repo guides

- [`ARCHITECTURE.md`](ARCHITECTURE.md) — high-level architecture overview.
- [`docs/api/README.md`](docs/api/README.md) — HTTP API reference generated from
  the C++ `RouteRegistry`.
- [`docs/configuration.md`](docs/configuration.md) — full configuration reference
  for server, TLS, logging, hardening, metrics, and storage knobs.
- [`docs/backend-selection.md`](docs/backend-selection.md) — storage backend
  comparison, maturity levels, failure modes, and migration notes.
- [`docs/storage/configuration.md`](docs/storage/configuration.md) — YAML storage
  config reference with examples for in-memory, SQLite, PostgreSQL, and Redis.
- [`docs/storage/how-to-add-adapter.md`](docs/storage/how-to-add-adapter.md) —
  step-by-step guide for adding a new storage backend.
- [`docs/analytics.md`](docs/analytics.md) — analytics pipeline, privacy model,
  and output interpretation.
- [`docs/benchmarking.md`](docs/benchmarking.md) — redirect benchmarking guide.
- [`docs/deployment.md`](docs/deployment.md) — deployment and operational guide.
- [`scripts/setup_ubuntu_dependencies.sh`](scripts/setup_ubuntu_dependencies.sh) —
  Ubuntu dependency bootstrap.
