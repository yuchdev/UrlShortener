# Architecture

> **Architecture Decision Records:**
>
> | ADR | Title | Status |
> |-----|-------|--------|
> | [ADR 0001](docs/adr/0001-cli-rest-shared-command-layer.md) | CLI/REST Shared Command Layer | Accepted (amended by 0002) |
> | [ADR 0002](docs/adr/0002-unified-command-model.md) | Unified Command Model (JSON commands and jobs, pluggable transports) | Implemented |
> | [ADR 0003](docs/adr/0003-tui-framework-and-architecture.md) | TUI Framework and Architecture | Implemented |
>
> Earlier references to ADRs on system architecture, backend topology, performance contract,
> security model and SOCI persistence pointed at files that were never committed; their
> content lives in `docs/backend-selection.md`, `docs/storage/`, `docs/configuration.md`, and `docs/security/`.

This document describes the project structure and modular runtime architecture of the
low-latency URL shortener.

## 1) High-level architecture

The service is a single-process asynchronous network server built with a modular design:

- **Transport/runtime**: Boost.Asio + Boost.Beast.
- **Security**: OpenSSL-backed TLS context for HTTPS.
- **Core**: Domain types (`Link`, `ClickEvent`) and utilities in `include/url_shortener/core/`.
- **HTTP**: Server, session handling, and request processing in `include/url_shortener/http/`.
- **Storage**: Pluggable adapters (`IMetadataRepository`, `ICacheStore`, `IRateLimiter`,
  `IClickEventRepository`) under `include/url_shortener/storage/`; wired at startup by
  `composition::InitLinkBackend` into an `ILinkStore` shared by every mode.
- **Analytics**: Asynchronous event queue in `include/url_shortener/analytics/`.
- **Command model**: Unified JSON command registry and job runner in
  `include/url_shortener/commands/`; REST, CLI, stdio, and TUI are transports over it
  (ADR 0002). See [`docs/commands/README.md`](docs/commands/README.md).
- **Terminal UI**: Optional FTXUI-backed interactive client in `src/tui/` (ADR 0003,
  built when `URLSHORTENER_ENABLE_TUI=ON`).

Data flow:

1. `main.cpp` parses CLI flags into `ServerConfig`.
2. `composition::InitLinkBackend` selects the storage backend and installs it as the
   process-wide `app::LinkBackend::store()`.
3. `HttpServer` is constructed and starts HTTP/HTTPS accept loops.
4. For each connection, `PlainSession` or `TlsSession` handles requests asynchronously;
   `handleShortenerRequest()` delegates to the application `Router`.
5. `RouterBuilder` registers handlers in priority order: observability →
   canonical `/api/v1/links` → compatibility `/api/v1/short-urls` → redirects →
   generic URI-store fallback. Resource and generic command routes call
   `CommandRegistry::execute`; redirect routes call `ILinkStore` directly (fast path).
6. CLI (`link`, `cmd`), `serve-stdio`, and `tui` are other entry points that share
   the same `CommandRegistry` built by `composition::build_command_runtime`
   (ADR 0002 — one registry, no mode builds its own).
7. Analytics events are enqueued to `BoundedClickEventQueue` for background processing.

## 2) Source layout

```text
.
├── include/url_shortener/
│   ├── core/              # domain types, config, utils, clock
│   ├── http/              # server, sessions, Router, RouteRegistry, handlers/
│   ├── app/               # ILinkStore, LinkBackend, LinkCommandService, table sources
│   ├── storage/           # IMetadataRepository, ICacheStore, IRateLimiter,
│   │                      # IClickEventRepository; inmemory/, sql/, sqlite/,
│   │                      # postgres/, redis/ adapters
│   ├── analytics/         # BoundedClickEventQueue, AnalyticsWorker
│   ├── commands/          # envelope, arg_spec, command_registry, job_runner,
│   │                      # transport_adapter, link_commands, browse_commands,
│   │                      # pair_commands, json_scan, catalog
│   ├── transport/         # stdio_adapter
│   ├── tui/               # command_client, in_process_client, screens/
│   ├── composition/       # command_runtime, link_backend_bootstrap, storage_factory
│   ├── cli/               # cli_parser, serve_stdio_dispatch
│   ├── security/          # AccessGuard, ControlSet
│   ├── observability/     # storage_observability (redactSecretValue)
│   └── config/            # storage_config, server_config types
├── src/
│   ├── core/              # utils.cpp
│   ├── http/              # server, sessions, request_handlers, handlers/
│   ├── app/               # link_backend, link_command_service, repository_link_store, table sources
│   ├── storage/           # adapter implementations
│   ├── analytics/         # analytics_worker, bounded_click_event_queue, analytics_service
│   ├── commands/          # all command implementations, json_scan, ndjson session
│   ├── transport/         # stdio_adapter.cpp
│   ├── tui/               # app, screens/, models, field_parsers, theme
│   ├── composition/       # command_runtime, link_backend_bootstrap, storage_factory
│   ├── cli/               # arg_coercion, cmd_dispatch, link_command_dispatch
│   ├── security/          # access_guard.cpp, control_set.cpp
│   ├── observability/     # storage_observability.cpp
│   ├── config/            # storage_config.cpp
│   └── main.cpp
├── tests/
│   ├── unit/              # commands/, tui/, http/, observability/, cli/, ...
│   ├── contract/          # metadata/ (IMetadataRepository contract suite)
│   ├── integration/       # sqlite/, psql/, redis/, analytics/
│   ├── conformance/       # commands/*.json corpus, runners/, py/ harness
│   ├── perf/              # analytics/, redirect/
│   └── e2e/               # scripts/ (POSIX-only shell tests)
└── docs/
    ├── adr/               # ADR 0001-0003
    ├── api/               # generated HTTP API reference
    ├── commands/          # command catalog, adding-a-transport.md
    ├── cli/               # CLI reference
    ├── tui/               # TUI user guide and development guide
    └── storage/           # backend guides
```

## 3) Runtime components

### 3.1 Entrypoint and lifecycle (`src/main.cpp`)

Responsibilities: parse CLI options into `ServerConfig`; call
`composition::InitLinkBackend` to select and install the storage backend; initialize
`boost::asio::io_context`; manage process signals (`SIGINT`, `SIGTERM`, `SIGHUP`).

### 3.2 Server and sessions (`src/http/`)

`HttpServer` manages listeners and spawns `PlainSession` or `TlsSession` objects for
the asynchronous read/write loop per connection. The HTTP server always binds
`0.0.0.0`; there is no bind-address flag yet (deferred to Milestone 0007). Because
the command API grants Admin to loopback peers, deployments must firewall the port.

### 3.3 Request handling and route metadata

`handleShortenerRequest()` dispatches through the application `Router`. `RouterBuilder`
registers routes in priority order: observability, canonical `/api/v1/links`,
compatibility `/api/v1/short-urls`, redirects, then generic URI-store fallback.

`RouteRegistry` is the C++ source of truth for route inventory, operation IDs,
compatibility aliases, and generated API documentation
([`docs/api/README.md`](docs/api/README.md)).

Redirect handlers for `GET /{slug}` and `GET /r/{slug}` are a protected fast path:
validate slug, fetch link via `app::LinkBackend::store()`, evaluate state,
update stats best-effort, return redirect response. Management-plane logic stays in
the `/api/v1/links` handlers. The redirect path does not depend on the command layer
(enforced by test `commands__80_redirect_isolation_test`).

### 3.4 Storage and analytics

`composition::InitLinkBackend` selects the storage backend from `StorageConfig` at
startup and installs it as the process-wide `app::LinkBackend::store()`. All modes
— redirects, REST handlers, CLI, and TUI — read and write the same backend through
this single `ILinkStore`. Analytics events are offloaded to `BoundedClickEventQueue`
to preserve redirect latency; `AnalyticsWorker` drains the queue in the background.

### 3.5 Command model and transports

Every link-management operation is a JSON command defined once in
`include/url_shortener/commands/` and executed by `CommandRegistry::execute`.
The pipeline is: validate envelope → authorize (`AccessGuard`) → `ArgSpec` check →
handler → `app::LinkCommandService`. Transports (REST, CLI `cmd`, stdio, TUI) only
differ in how the JSON envelope moves in and out; they contain no per-command logic.

The normative command catalog is [`docs/commands/README.md`](docs/commands/README.md)
(ADR 0002); `docs/commands/adding-a-transport.md` describes `ITransportAdapter`.

The command API (`POST /api/v1/commands`) is off by default and enabled with
`--command-api-enabled`. The `db.*` commands expose exactly three browsable tables:
`links`, `click_events`, and `jobs`.

### 3.6 Jobs

Long-running operations (`link.import`, `link.export`) are dispatched asynchronously
via the process-wide `JobRunner` (a bounded thread-pool, in-memory state, one
instance per process). The `job.*` commands query and cancel jobs. A command
response with `"status": "accepted"` carries a job ID; the caller polls `job.get`
or `GET /api/v1/jobs/{id}`.

### 3.7 Terminal UI

The optional FTXUI terminal UI (`src/tui/`, built when
`-DURLSHORTENER_ENABLE_TUI=ON`) provides an interactive client over the shared
`CommandRegistry` through the `ICommandClient` seam. No business logic lives in
TUI code; no service or repository headers are included under `src/tui/`. No HTTP
server is started and no socket is bound in TUI mode. See
[`docs/tui/README.md`](docs/tui/README.md) (user and development guide) and ADR 0003
([docs/adr/0003-tui-framework-and-architecture.md](docs/adr/0003-tui-framework-and-architecture.md)).
The `--read-only` flag downgrades the TUI session to `ControlSet::User`.

## 4) Build/test system

- CMake-based build with `sources.cmake` managing the modular file list.
  TUI sources are inside a `URLSHORTENER_ENABLE_TUI`-guarded block; `commands/`
  and `transport/` sources are always compiled.
- `-DURLSHORTENER_ENABLE_TUI=ON|OFF` (default `ON`); FTXUI is fetched via
  `FetchContent` — no manual install needed.
- CTest labels: `unit`, `contract`, `integration`, `e2e` (POSIX-only, toggle with
  `-DURLSHORTENER_ENABLE_E2E_TESTS=OFF`), `perf`, and `conformance` (cross-transport
  corpus under `tests/conformance/`).
- Drift tests: `commands__12_catalog_drift` (command catalog block 3.7),
  `docs__05_api_reference_matches_registry` (API reference).
- See [`docs/testing/README.md`](docs/testing/README.md) for the full test-plan index.
