# Architecture

This document describes the **current** project structure and runtime architecture, plus how it maps to the planned low-latency URL shortener direction.

## 1) High-level architecture (current)

The service is a single-process asynchronous network server:

- **Transport/runtime**: Boost.Asio + Boost.Beast.
- **Security**: OpenSSL-backed TLS context for HTTPS.
- **Application logic**: request routing + handlers in `src/url_shortener.cpp`.
- **Storage**: process-local singleton map (`UriMapSingleton`) with best-effort file persistence (`uri.txt`) at shutdown.

Data flow today:

1. `main.cpp` parses CLI flags into `ServerConfig`.
2. `HttpServer` is constructed and starts HTTP/HTTPS accept loops.
3. For each connection, a session reads one request, builds one response, and closes.
4. Request handlers dispatch to:
   - Stage-2 shortener routes (`/api/v1/short-urls`, `/r/{code}`), or
   - legacy key-value URI routes (`/{uri}`).
5. Storage calls go through `UriMapSingleton`.

## 2) Source layout

```text
.
├── include/url_shortener/
│   ├── url_shortener.h          # public server/tls config + HttpServer interface
│   └── uri_map_singleton.h    # in-memory singleton storage interface
├── src/
│   ├── main.cpp               # process entrypoint, args, signals, persistence lifecycle
│   ├── url_shortener.cpp        # protocol handling, routing, TLS setup, sessions
│   └── uri_map_singleton.cpp  # storage implementation + file serialize/deserialize
├── test/http_client_test/
│   └── http_client_test.py    # integration checks (HTTP/HTTPS behavior)
├── docs/roadmap/
│   ├── 01-STAGE_URL_SHORTENER_CORE.md
│   ├── 01-STAGE_HTTPS_IMPLEMENTATION_REPORT.md
│   ├── 02-STAGE_API_IMPLEMENTATION_REPORT.md
│   └── 02-STAGE_LINK_MANAGEMENT_FEATURES.md
├── STAGE_03_TECHNICAL_SPEC.md
├── STAGE_04_TECHNICAL_SPEC.md
├── STAGE_05_TECHNICAL_SPEC.md
├── CMakeLists.txt
└── sources.cmake
```

## 3) Runtime components

### 3.1 Entrypoint and lifecycle (`src/main.cpp`)

Responsibilities:

- Parse CLI options into `ServerConfig` + `TlsConfig`.
- Initialize `boost::asio::io_context`.
- Restore persisted in-memory map from `uri.txt` (if present).
- Start HTTP/HTTPS listeners via `HttpServer::run()`.
- Handle process signals:
  - `SIGINT`/`SIGTERM`: stop server, persist map, stop io context.
  - `SIGHUP` (unix): reload TLS context.

### 3.2 Server and networking (`src/url_shortener.cpp`)

Responsibilities:

- Build and validate TLS context.
- Start accept loops for HTTP and optional HTTPS.
- Spawn session objects (`PlainSession`, `TlsSession`) per accepted socket.
- Parse HTTP requests and construct responses.
- Route requests between shortener endpoints and legacy storage endpoints.

Notable behavior:

- Optional HTTP-to-HTTPS redirect mode.
- Optional HSTS on TLS responses.
- TLS handshake success/failure counters, emitted during shutdown.

### 3.3 Request handling model

`handleShortenerRequest(...)` handles:

- `POST /api/v1/short-urls`
- `GET /api/v1/short-urls/{code}`
- `DELETE /api/v1/short-urls/{code}`
- `GET /r/{code}`

Fallback dispatch goes to `handleApplicationRequest(...)` for legacy `GET/POST/DELETE /{uri}` map operations.

### 3.4 Storage (`UriMapSingleton`)

Current storage model:

- `std::unordered_map<std::string, std::pair<std::string, std::string>>`
- keys are URI-like strings.
- shortener records are stored under `/r/{code}` path keys with JSON payload body strings.
- serialized to plain text file format with a simple marker (`RecordEnd`).

Concurrency/performance implications:

- Singleton map is not protected by mutexes in current implementation.
- Data is process-local and non-shared across instances.
- Suitable for early-stage prototypes but not for horizontally scaled production.

## 4) Build/test system

- CMake-based build (`CMakeLists.txt`, `sources.cmake`).
- Boost bootstrap helper scripts (`boost.sh`, `boost.cmd`, `cmake/fetch-boost.cmake`).
- Basic Python integration test script in `test/http_client_test/http_client_test.py`.

## 5) Architectural gaps vs low-latency target

To reach the intended low-latency shortener architecture, key changes are expected:

- Introduce explicit `Link` domain model and repository abstraction.
- Replace singleton map with thread-safe storage interface (in-memory + pluggable durable backend).
- Move from ad-hoc JSON string parsing to robust serialization/validation strategy.
- Add dedicated redirect fast path with measured latency budgets.
- Add metrics/tracing and repeatable performance tests in CI.

## 6) Suggested near-term evolution path

1. **Domain boundary extraction**
   - Create `Link`, validator, slug policy, and repository interface.
2. **Storage abstraction**
   - Keep in-memory adapter first, then add durable adapter behind the same interface.
3. **Routing split**
   - Separate legacy key-value routes from shortener routes into explicit modules.
4. **Latency hardening**
   - Add benchmark harness and optimize redirect path for allocations/locks.
5. **Ops hardening**
   - Expand observability, health checks, and rollout safety knobs.
