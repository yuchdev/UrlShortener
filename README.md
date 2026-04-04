# Low-latency URL shortener

This repository currently contains a C++17 HTTP/HTTPS service that started as a key-value URI store and now includes initial URL shortener endpoints.

The intended direction is to evolve this service into a **low-latency URL shortener** with predictable redirect performance, robust operational controls, and clearer domain boundaries.

## Current state

### What exists today

- Single binary server (`url_shortener`) built with Boost.Asio/Beast + OpenSSL.
- HTTP listener and optional HTTPS listener.
- Optional HTTP -> HTTPS redirect (`308`).
- TLS hardening controls (TLS version/ciphers/curves/session config), optional mTLS, and SIGHUP TLS context reload.
- Shortener API:
  - `POST /api/v1/short-urls` create mapping (custom or generated code).
  - `GET /r/{code}` resolve + redirect (`302`).
  - `GET /api/v1/short-urls/{code}` read metadata.
  - `DELETE /api/v1/short-urls/{code}` delete mapping.
- In-memory data storage persisted to local `uri.txt` on shutdown and loaded on startup.

### Current limitations

- No dedicated Link domain model yet (records are stored as JSON strings in a generic URI map).
- No pluggable storage abstraction yet (only singleton in-memory map).
- No latency-focused benchmark + profiling loop committed as part of CI.
- Minimal JSON parsing/validation strategy (string scanning) suitable for early stage but not ideal for long-term correctness/performance.
- No production observability stack (only startup/shutdown logs and handshake counters).

That means:

* **POST** for link creation
* **GET** for reads, preview, stats, and redirect
* **PATCH** for management updates
* **DELETE** for soft delete
* optional **POST** for action-style endpoints like `/enable`, `/disable`, `/restore`

### Why not switch away from the current stack now

Your actual bottlenecks are elsewhere:

* storage is still a process-local singleton
* records are stored as ad-hoc JSON strings
* thread safety is incomplete
* repository/domain boundaries are not extracted yet
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

## Ubuntu dependency baseline (Stage 0)

Canonical environment: **Ubuntu 24.04 LTS** (best-effort 22.04).

Install build/test dependencies with:

```bash
./scripts/setup_ubuntu_dependencies.sh
```

This installs all required Stage 0 packages through `apt` (including Boost and OpenSSL) and replaces the old Boost-from-source flow for Ubuntu.

See `docs/setup/UBUNTU.md` for package details and troubleshooting.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target url_shortener
```

## Run

### Show all options

```bash
./build/url_shortener --help
```

### HTTP only

```bash
./build/url_shortener
```

### HTTP only on a custom port (positional shorthand)

```bash
./build/url_shortener 9090
```

### HTTPS enabled

```bash
./build/url_shortener \
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
ctest --test-dir build --output-on-failure
```

(Direct invocation still works: `python3 test/http_client_test/http_client_test.py`.)

## Repo guides

- `ARCHITECTURE.md` - current architecture and layout.
- `docs/storage/overview.md` - Stage 03 storage abstraction (scope 1) overview.
- `docs/stages/` - staged specification documents.
- `docs/setup/UBUNTU.md` - Ubuntu dependency/bootstrap guide.
