# Architecture

This document describes the project structure and modular runtime architecture of the low-latency URL shortener.

## 1) High-level architecture

The service is a single-process asynchronous network server built with a modular design:

- **Transport/runtime**: Boost.Asio + Boost.Beast.
- **Security**: OpenSSL-backed TLS context for HTTPS.
- **Core**: Domain types (`Link`, `ClickEvent`) and utilities in `include/url_shortener/core/`.
- **HTTP**: Server, session handling, and request processing in `include/url_shortener/http/`.
- **Storage**: Pluggable metadata repository (`IMetadataRepository`) and cache interface (`ILinkCacheStore`) with in-memory implementations in `include/url_shortener/storage/`.
- **Analytics**: Asynchronous event queue for tracking redirect performance in `include/url_shortener/analytics/`.

Data flow:

1. `main.cpp` parses CLI flags into `ServerConfig`.
2. `HttpServer` is constructed and starts HTTP/HTTPS accept loops.
3. For each connection, a session (`PlainSession` or `TlsSession`) handles requests asynchronously.
4. `handleShortenerRequest()` delegates to the application `Router`; route
   handlers in `src/http/handlers/` own observability, link, compatibility,
   redirect, and fallback behavior.
5. Storage calls are handled by the active `IMetadataRepository` implementation.
6. Analytics events are enqueued to `BoundedClickEventQueue` for background processing.

## 2) Source layout

```text
.
├── include/url_shortener/
│   ├── core/
│   │   ├── types.h              # domain models and enums
│   │   ├── config.h             # server and TLS configuration structs
│   │   └── utils.h              # string, time, and validation helpers
│   ├── http/
│   │   ├── http_server.h        # main server lifecycle
│   │   ├── http_session.h       # async session handling
│   │   ├── RouteRegistry.hpp     # route metadata source of truth
│   │   ├── Router.hpp            # small in-process router
│   │   ├── handlers/             # route-specific handlers
│   │   └── request_handlers.h    # HTTP entry point and shared helpers
│   ├── storage/
│   │   └── link_repository.h    # repository interface and singleton
│   ├── analytics/
│   │   └── click_event_queue.h  # metrics collection queue
│   └── url_shortener.h          # public facade (includes modular headers)
├── src/
│   ├── core/
│   │   └── utils.cpp            # utility implementations
│   ├── http/
│   │   ├── http_server.cpp
│   │   ├── http_session.cpp
│   │   └── request_handlers.cpp
│   ├── storage/
│   │   └── link_repository.cpp
│   ├── analytics/
│   │   └── click_event_queue.cpp
│   └── main.cpp                 # process entrypoint
├── tests/
│   └── unit/                    # modular unit tests
└── ARCHITECTURE.md
```

## 3) Runtime components

### 3.1 Entrypoint and lifecycle (`src/main.cpp`)

Responsibilities:
- Parse CLI options into `ServerConfig`.
- Initialize `boost::asio::io_context`.
- Restore persisted data if applicable.
- Manage process signals (`SIGINT`, `SIGTERM`, `SIGHUP`).

### 3.2 Server and sessions (`src/http/`)

The `HttpServer` manages the lifecycle of listeners. It spawns `PlainSession` or `TlsSession` objects which handle the asynchronous read/write loop for each connection.

### 3.3 Request handling and route metadata

`handleShortenerRequest()` is intentionally thin and dispatches through the
application `Router`. `RouterBuilder` registers routes in priority order:
observability, canonical `/api/v1/links`, compatibility `/api/v1/short-urls`,
redirects, then generic URI-store fallback.

`RouteRegistry` is the C++ source of truth for route inventory, labels,
operation IDs, compatibility aliases, placeholder routes, and generated API
documentation. The checked-in API reference lives at `docs/api/README.md`.

Redirect handlers for `GET /{slug}` and `GET /r/{slug}` remain a protected fast
path: validate slug, read link, evaluate state, update stats/analytics
best-effort, and return the redirect response. Management-plane logic stays in
the `/api/v1/links` handlers.

### 3.4 Storage and Analytics

Domain logic interacts with storage through the `linkRepository()` singleton. Performance-sensitive metrics are offloaded to the `analyticsQueue()` to ensure low-latency redirect responses.

## 4) Build/test system

- CMake-based build using `sources.cmake` to manage the modular file list.
- Comprehensive unit test suite covering core utilities, validation, and link management.
- Integration tests via Python script for full-stack verification.
