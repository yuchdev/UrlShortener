# 0001 REST API Refactoring

## Background

The URL shortener originally routed every HTTP request through
`src/http/request_handlers.cpp::handleShortenerRequest()`, a single function
that mixed route matching, method validation, path parsing, response
construction, link-management behavior, redirect behavior, analytics
emission, and fallback URI-store behavior in one ordered branch chain. A first
slice had already introduced an explicit route registry
(`RouteRegistry`/`registeredRoutes()`), but dispatch itself remained the old
branch chain.

This milestone replaces that branch chain with a small in-house router
(`Router`/`RouterBuilder`) built on the existing Boost.Beast request/response
types, using `RouteRegistry` metadata as the single source of truth for route
enumeration, dispatch, and generated documentation - without introducing an
external REST framework or a new JSON dependency.

See
[rest-api-refactoring.md](/docs/roadmap/0001-rest-api-refactoring/rest-api-refactoring.md)
for the full design rationale, the framework candidate comparison (Oat++,
Drogon, RESTinio, Crow, Pistache, cpp-httplib vs. an in-house router), the
complete endpoint inventory, the testing plan, the risk analysis, and the
rollback plan.

## Architecture

Target request flow:

```text
PlainSession / TlsSession
  -> existing transport hardening
  -> handleShortenerRequest(req, config, is_tls)
     -> applicationRouter().dispatch(req, config, is_tls)
        -> match method + path pattern (Router::matchPath)
        -> extract RouteContext (path params, query string, route label)
        -> call handler
     -> response
  -> existing metrics/logging/write
```

`handleShortenerRequest()` is now a one-line delegation:

```cpp
bhttp::response<bhttp::string_body> handleShortenerRequest(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config,
    const bool is_tls)
{
    return applicationRouter().dispatch(req, config, is_tls);
}
```

(`src/http/request_handlers.cpp`)

Boundaries:

- `Router` (`include/url_shortener/http/router.hpp`, `src/http/router.cpp`)
  matches and dispatches; it does not know link business rules.
- `RouterBuilder` (`include/url_shortener/http/router_builder.hpp`,
  `src/http/router_builder.cpp`) is the only file that knows registration
  order.
- Handler files under `src/http/handlers/` own behavior; they do not do route
  table ordering.
- `RouteRegistry` (`include/url_shortener/http/route_registry.hpp`,
  `src/http/route_registry.cpp`) owns route metadata, labels, and
  documentation fields.
- `request_handlers.cpp` keeps shared response helpers and the public
  `handleShortenerRequest()` entry point.

## Tasks

| Task | Name | Category | Output |
|------|------|----------|--------|
| 01.0 | [Baseline characterization](/docs/roadmap/0001-rest-api-refactoring/01.0-baseline-characterization/README.md) | Testing | Behavior-preserving characterization tests locking the endpoint matrix, wrong-method/error responses, and redirect/fallback behavior before any dispatch change. |
| 02.0 | [Router infrastructure](/docs/roadmap/0001-rest-api-refactoring/02.0-router-infrastructure/README.md) | Infrastructure | `RouteContext`, handler type aliases, `Router` path matching + dispatch, `RouterBuilder`, and registry/router consistency checks - no production behavior change. |
| 03.0 | [Observability routes](/docs/roadmap/0001-rest-api-refactoring/03.0-observability-routes/README.md) | Migration | `handleHealthz`/`handleReadyz`/`handleMetrics` extracted, registered, and dispatched through `Router` - first production router usage. |
| 04.0 | [Link management routes](/docs/roadmap/0001-rest-api-refactoring/04.0-link-management-routes/README.md) | Migration | Canonical `/api/v1/links` read, preview, create, patch, delete, lifecycle-action, stats, and placeholder handlers extracted, registered, and dispatched through `Router`. |
| 05.0 | [Compatibility routes](/docs/roadmap/0001-rest-api-refactoring/05.0-compatibility-routes/README.md) | Migration | `/api/v1/short-urls` compatibility create/read handlers extracted, registered, and dispatched through `Router`. |
| 06.0 | [Redirect routes](/docs/roadmap/0001-rest-api-refactoring/06.0-redirect-routes/README.md) | Migration | `GET /r/{slug}` and `GET /{slug}` handlers extracted and dispatched through `Router` without broadening the fast path. |
| 07.0 | [Fallback routes](/docs/roadmap/0001-rest-api-refactoring/07.0-fallback-routes/README.md) | Migration | Generic URI-store fallback wrapped, registered last, dispatched through `Router`; `handleApplicationRequest()` boundary clarified. |
| 08.0 | [OpenAPI/docs cleanup](/docs/roadmap/0001-rest-api-refactoring/08.0-openapi-docs-cleanup/README.md) | Cleanup | Extended `RouteDescriptor` documentation metadata, generated API reference (`docs/api/README.md`), obsolete branch-chain removal, and public docs/review sync. |

## Shared contracts (authoritative)

Cross-cutting rules every task must honor:

**C1. Session lifecycle is out of scope.** Keep `src/http/http_session.cpp`
session lifecycle unchanged. `PlainSession`, `TlsSession`, TLS reloads, and the
async accept/read/write lifecycle are not touched by this milestone.

**C2. `handleShortenerRequest()` stays the public entry point.** It remains the
single production entry point throughout the migration; only its
implementation moves from a branch chain to `applicationRouter().dispatch(...)`.

**C3. Preserve externally observable behavior.** Status codes, headers,
JSON/text response body shapes, error codes, request IDs, metrics labels, and
analytics side effects must not change unless a task explicitly says
otherwise.

**C4. Keep the redirect fast path narrow.** `/r/{slug}` and root `/{slug}`
redirect handling stays free of management-plane logic: no JSON link
serialization, no handler-header coupling to link-management handlers.

**C5. No external REST framework.** Do not adopt Oat++, Drogon, Crow,
RESTinio, Pistache, cpp-httplib, or any other web framework as part of this
milestone.

**C6. No new JSON dependency.** The existing manual JSON helpers remain the
approved parsing path.

**C7. C++17 only.**

**C8. Register every new `.cpp`** in `sources.cmake` and, when needed, in
`CMakeLists.txt`.

**C9. First-match-wins registration order is part of the API contract.**
Required order: exact observability paths -> most-specific canonical API
paths -> shorter canonical API paths -> compatibility API paths -> prefixed
redirect path -> root redirect path -> generic fallback paths. In particular:
`/api/v1/links/id/{id}` before `/api/v1/links/{slug}`; action routes (e.g.
`/api/v1/links/{slug}/stats`) before `/api/v1/links/{slug}`; `/r/{slug}`
before `/{slug}`; fallback `/{path}` last.

## Dependency graph

```text
01.0 baseline-characterization
   |  (locks current behavior; no dispatch change)
   v
02.0 router-infrastructure
   |  (RouteContext, Router, RouterBuilder; stub handlers only)
   v
03.0 observability-routes
   |  (first production router usage: /healthz, /readyz, /metrics)
   v
04.0 link-management-routes
   |  (canonical /api/v1/links fully router-backed)
   v
05.0 compatibility-routes
   |  (/api/v1/short-urls fully router-backed)
   v
06.0 redirect-routes
   |  (/r/{slug}, /{slug} router-backed; fast path protected)
   v
07.0 fallback-routes
   |  (generic URI-store fallback registered last)
   v
08.0 openapi-docs-cleanup
      (doc metadata, generated API reference, branch-chain removal, doc sync)
```

## File map

```text
include/url_shortener/http/
  route_context.hpp
  handler_types.hpp
  router.hpp
  router_builder.hpp
  route_registry.hpp
  request_handlers.h
  handlers/
    observability_handlers.hpp
    link_handlers.hpp
    compatibility_handlers.hpp
    redirect_handlers.hpp
    fallback_handlers.hpp

src/http/
  router.cpp
  router_builder.cpp
  route_registry.cpp
  request_handlers.cpp
  handlers/
    observability_handlers.cpp
    link_handlers.cpp
    compatibility_handlers.cpp
    redirect_handlers.cpp
    fallback_handlers.cpp

tools/
  route_registry_dump.cpp

docs/api/
  README.md

tests/unit/http/
  01_route_registry.cpp
  02_endpoint_matrix_characterization.cpp
  03_method_and_error_characterization.cpp
  04_redirect_fallback_characterization.cpp
  05_route_context.cpp
  06_router_matcher.cpp
  07_router_dispatch.cpp
  08_router_registry_consistency.cpp
  09_observability_handlers.cpp
  10_link_handlers.cpp
  11_compatibility_handlers.cpp
  12_prefixed_redirect_handler.cpp
  13_root_redirect_handler.cpp
  14_fallback_handler.cpp

tests/unit/docs/
  02_route_registry_docs.cpp
```

Implementation note: all new headers/sources shipped in `snake_case` (e.g.
`router.hpp`, `route_registry.cpp`, `handlers/link_handlers.cpp`), not the
PascalCase names originally proposed in the per-task specs (e.g.
`Router.hpp`, `RouteRegistry.cpp`, `handlers/LinkHandlers.cpp`). Commit
`3f4c170 Rename files to snake_style` renamed the tree after initial
implementation. Test numbering was also consolidated/renumbered relative to
the original per-task proposals - see each subtask's Implementation note for
the exact mapping.

## Validation commands

Use a normal repository-local build directory, not a CLion temp directory:

```powershell
cmake --build "C:\Users\atatat\Projects\UrlShortener\cmake-build" --target url_shortener --config Debug
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build" -C Debug -L unit --output-on-failure
ctest --test-dir "C:\Users\atatat\Projects\UrlShortener\cmake-build" -C Debug -L "unit|contract|integration|e2e" --output-on-failure
```

## Global acceptance criteria

- [x] Every endpoint in the inventory is represented in `registeredRoutes()`
      (`src/http/route_registry.cpp`).
- [x] Every dispatch route has a corresponding `RouteDescriptor`, and vice
      versa (enforced by `tests/unit/http/08_router_registry_consistency.cpp`).
- [x] `handleShortenerRequest()` no longer contains route-specific branch
      logic - it is a one-line delegation to `applicationRouter().dispatch(...)`
      (`src/http/request_handlers.cpp`).
- [x] Route handlers are named and testable independently
      (`src/http/handlers/*.cpp`).
- [x] The redirect fast path remains narrow and behavior-compatible
      (`src/http/handlers/redirect_handlers.cpp`; security review passed, see
      [docs/security/2026-07-17-redirect-route-refactor.md](/docs/security/2026-07-17-redirect-route-refactor.md)).
- [x] All route labels remain stable (covered by characterization tests).
- [x] Compatibility endpoints continue to work
      (`src/http/handlers/compatibility_handlers.cpp`).
- [x] Unit tests cover router matching, dispatch ordering, and handler
      behavior - 148/148 unit tests passing (`ctest -L unit`); the HTTP/router
      subset (`01_route_registry` through `14_fallback_handler`, 29 binaries)
      is 29/29 passing (`ctest -R "^0[1-9]_|^1[0-4]_" -L unit`).
- [x] The main binary builds with CMake/vcpkg on Windows (verified against the
      current `cmake-build/` build).
- [x] Focused HTTP/router CTest targets pass (see above).
- [x] Generated API docs can be derived from route metadata
      (`tools/route_registry_dump.cpp` -> `docs/api/README.md`).
- [x] No route has two active implementations after cleanup (Task 08.0).
- [x] Security review passed for both the general route refactor and the
      redirect-route change specifically:
      [docs/security/2026-07-17-rest-api-route-refactor.md](/docs/security/2026-07-17-rest-api-route-refactor.md)
      (Verdict: PASS),
      [docs/security/2026-07-17-redirect-route-refactor.md](/docs/security/2026-07-17-redirect-route-refactor.md)
      (Verdict: PASS).
