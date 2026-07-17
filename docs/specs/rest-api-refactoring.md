# REST API Refactoring Plan

Status: Draft for review  
Date: 2026-07-16  
Scope: HTTP routing and REST API handler organization

## 1. Summary

The URL shortener currently routes all HTTP requests through
`src/http/request_handlers.cpp::handleShortenerRequest()`. That function mixes
route matching, method validation, path parsing, response construction, link
management behavior, redirect behavior, analytics emission, and fallback URI
store behavior in one ordered branch chain.

The first refactoring slice already introduced an explicit route registry:

- `include/url_shortener/http/RouteRegistry.hpp`
- `src/http/RouteRegistry.cpp`
- `tests/unit/http/01_route_registry.cpp`

The registry enumerates routes and now owns route-label classification through
`routeLabelForTarget()`, while `request_handlers.cpp::routeLabelFor()` delegates
to it. Request dispatch itself is intentionally unchanged.

This document proposes the next refactoring stages. The default recommendation
is **not** to replace Boost.Beast with a full REST framework. Instead, keep the
current transport stack and introduce a small in-house router that uses the
existing `RouteRegistry` metadata as the single source of truth for route
enumeration, future documentation generation, and dispatch validation.

## 2. Goals

1. Make REST API route declarations explicit, reviewable, and enumerable from
   C++ code.
2. Gradually replace the monolithic dispatch chain in
   `handleShortenerRequest()` with a small router that matches method plus path
   pattern.
3. Extract route handlers into named functions or small handler objects that can
   be tested independently.
4. Preserve all externally observable behavior: status codes, headers, response
   bodies, error codes, request IDs, metrics labels, analytics side effects, and
   redirect behavior.
5. Keep the redirect fast path narrow and free of management-plane logic.
6. Keep the Boost.Asio + Boost.Beast + OpenSSL session lifecycle intact.
7. Avoid new runtime dependencies unless a later ADR explicitly approves them.
8. Enable future OpenAPI or API-reference generation from route metadata.

## 3. Non-goals

1. Do not migrate to Oat++, Drogon, Crow, RESTinio, Pistache, cpp-httplib, or
   another web framework in this plan.
2. Do not rewrite `HttpServer`, `PlainSession`, `TlsSession`, TLS reloads, or
   the async accept/read/write lifecycle.
3. Do not change canonical API shape. New work still targets
   `/api/v1/links/...`; `/api/v1/short-urls/...` remains compatibility.
4. Do not change redirect semantics for `GET /r/{slug}` or `GET /{slug}`.
5. Do not introduce a JSON library for request parsing. The current manual JSON
   helpers remain the approved path unless a separate dependency decision
   changes that.
6. Do not implement currently deferred features such as QR code generation or
   routing rules. Their current `501 feature_not_enabled` behavior must remain.
7. Do not add authentication or authorization as part of this refactor.
8. Do not touch storage backends, analytics storage, migrations, or cache/rate
   limiter contracts except through existing handler calls.

## 4. Current state

### 4.1 Transport and request lifecycle

Current request flow:

```text
HttpServer
  -> PlainSession or TlsSession
     -> request target/body limit checks
     -> optional HTTP-to-HTTPS redirect
     -> handleShortenerRequest(req, config, is_tls)
     -> record metric using routeLabelFor(target)
     -> structured request-completion log
     -> async_write(response)
```

Important existing guarantees:

- oversized target returns `414 target_too_large`;
- oversized body returns `413 payload_too_large`;
- inbound `X-Request-Id` is accepted only if valid and length-bounded;
- generated or accepted request id is propagated to responses;
- metrics labels are stable strings;
- TLS behavior remains in the server/session layer;
- analytics failures must never affect redirect responses.

### 4.2 Main router today

`src/http/request_handlers.cpp::handleShortenerRequest()` currently handles:

1. `GET /healthz`
2. `GET /readyz`
3. `GET /metrics`
4. link lookup by id
5. link lookup/update/delete by slug
6. link preview
7. link stats
8. placeholder QR/routing endpoints
9. enable/disable/restore actions
10. compatibility short-url create/read endpoints
11. prefixed redirects under `/r/{slug}`
12. root redirects under `/{slug}`
13. generic URI store fallback through `handleApplicationRequest()`

The code is ordered by string-prefix checks. Several routes are not represented
as route declarations; they are implicit in nested `action` parsing.

### 4.3 RouteRegistry slice already completed

The registry currently provides:

```cpp
struct RouteDescriptor
{
    std::string method;
    std::string path_pattern;
    std::string route_label;
    std::string summary;
};

const std::vector<RouteDescriptor>& registeredRoutes();
std::string routeLabelForTarget(const std::string& target);
```

This is useful but incomplete. `registeredRoutes()` enumerates the endpoint
matrix, while dispatch still lives in the old branch chain. The next stages
should make dispatch and handler extraction consume the same route concepts.

## 5. Framework and dependency decision

### 5.1 Candidate summary

| Candidate | Strength | Main issue for this codebase |
|---|---|---|
| Oat++ | Best fit for C++ endpoint declarations and OpenAPI/Swagger support. | Would move the project toward a framework-owned server/router model and require a larger transport migration. |
| Drogon | Mature C++ web framework with controller-style declarations. | Own application/server model; heavier than needed for this routing-only refactor. |
| RESTinio | Asio-oriented and explicit route registration. | Does not solve OpenAPI/documentation generation enough to justify replacement. |
| Crow | Simple route macros. | Weak documentation generation and framework/server replacement risk. |
| Pistache | Explicit REST router style. | Windows/vcpkg integration risk and insufficient documentation-from-code value. |
| cpp-httplib | Lightweight HTTP server/client. | Would replace Beast transport and does not provide the needed route metadata model. |
| In-house RouteRegistry + Router | Exact fit for current Beast stack and constraints. | We own a small path matcher and must test it thoroughly. |

### 5.2 Recommendation

Use an in-house router built on the existing Boost.Beast request/response
types. Keep third-party frameworks out of the default implementation.

Reasons:

- current sessions already own TLS, timeouts, request hardening, metrics, and
  logging;
- the needed router behavior is small and predictable;
- no new vcpkg package is required;
- the redirect fast path can remain tightly controlled;
- route metadata can become the documentation source without changing
  transport.

If the team later decides that runtime Swagger UI or full OpenAPI integration is
mandatory, evaluate Oat++ in a separate spike and ADR. That should be treated as
a server-stack migration, not as a continuation of this refactor.

## 6. Endpoint inventory

The refactor must preserve the following endpoint matrix.

| Group | Method | Path | Label | Current behavior |
|---|---|---|---|---|
| Observability | GET | `/healthz` | `healthz` | Returns `200 ok`. |
| Observability | GET | `/readyz` | `readyz` | Returns `200 ok`. |
| Observability | GET | `/metrics` | `metrics` | Returns text metrics from `renderMetrics()`. |
| Links | POST | `/api/v1/links` | `api_links` | Creates a short link. |
| Links | GET | `/api/v1/links/id/{id}` | `api_links` | Reads link by internal id. |
| Links | GET | `/api/v1/links/{slug}` | `api_links` | Reads link by slug. |
| Links | PATCH | `/api/v1/links/{slug}` | `api_links` | Updates mutable management fields. |
| Links | DELETE | `/api/v1/links/{slug}` | `api_links` | Soft-deletes link. |
| Links | GET | `/api/v1/links/{slug}/preview` | `api_links` | Returns preview JSON without redirect. |
| Links | GET | `/api/v1/links/{slug}/qr` | `api_links` | Placeholder, returns `501 feature_not_enabled`. |
| Links | GET | `/api/v1/links/{slug}/routing` | `api_links` | Placeholder, returns `501 feature_not_enabled`. |
| Links | GET | `/api/v1/links/{slug}/stats` | `api_links` | Returns link counters or analytics aggregates with query parameters. |
| Links | POST | `/api/v1/links/{slug}/enable` | `api_links` | Enables link. |
| Links | POST | `/api/v1/links/{slug}/disable` | `api_links` | Disables link. |
| Links | POST | `/api/v1/links/{slug}/restore` | `api_links` | Clears soft-delete marker. |
| Compatibility | POST | `/api/v1/short-urls` | `api_links` | Compatibility create endpoint. |
| Compatibility | GET | `/api/v1/short-urls/{slug}` | `api_links` | Compatibility read endpoint. |
| Redirect | GET | `/r/{slug}` | `redirect_prefixed` | Prefixed redirect fast path. |
| Redirect | GET | `/{slug}` | `redirect_root` | Root redirect fast path; may fall back to URI store semantics. |
| Fallback | GET | `/{path}` | `redirect_root` for non-root paths | Generic URI store read. |
| Fallback | POST | `/{path}` | `redirect_root` for non-root paths | Generic URI store write. |
| Fallback | DELETE | `/{path}` | `redirect_root` for non-root paths | Generic URI store delete. |
| Fallback | GET | `/` | `app` | Generic URI store root read. |
| Fallback | POST | `/` | `app` | Generic URI store root write. |
| Fallback | DELETE | `/` | `app` | Generic URI store root delete. |

Notes:

- Existing unsupported methods currently tend to return `400 invalid_method` or
  generic fallback errors. Preserve exact behavior until an explicit later stage
  changes method handling.
- `GET /api/v1/links/{slug}/stats` has two modes:
  - with `from`, `to`, and `bucket`: delegates to `AnalyticsStatsHandler`;
  - without the full query set: returns legacy link counters.
- Compatibility endpoint behavior must remain stable for existing clients.

## 7. Target architecture

### 7.1 High-level flow

```text
PlainSession / TlsSession
  -> existing transport hardening
  -> handleShortenerRequest(req, config, is_tls)
     -> route label from RouteRegistry
     -> Router::dispatch(req, config, is_tls)
        -> match method + path pattern
        -> extract RouteContext
        -> call handler
     -> response
  -> existing metrics/logging/write
```

### 7.2 New concepts

Proposed new types:

```cpp
namespace url_shortener::http {

using BeastRequest =
    boost::beast::http::request<boost::beast::http::string_body>;

using BeastResponse =
    boost::beast::http::response<boost::beast::http::string_body>;

using PathParams = std::unordered_map<std::string, std::string>;

struct RouteContext
{
    PathParams path_params;
    std::string query_string;
    std::string route_label;
};

using HandlerFn = std::function<BeastResponse(
    const BeastRequest&,
    const ServerConfig&,
    bool is_tls,
    const RouteContext&)>;

}
```

Proposed files:

```text
include/url_shortener/http/
  RouteContext.hpp
  Router.hpp
  RouterBuilder.hpp
  HttpHandlers.hpp

src/http/
  Router.cpp
  RouterBuilder.cpp
  handlers/
    ObservabilityHandlers.cpp
    LinkHandlers.cpp
    CompatibilityHandlers.cpp
    RedirectHandlers.cpp
    FallbackHandlers.cpp
```

The exact file split can be adjusted during implementation, but the design
should preserve these boundaries:

- `Router` matches and dispatches. It does not know link business rules.
- handler files own behavior. They do not do route table ordering.
- `RouteRegistry` owns route metadata and labels.
- `request_handlers.cpp` keeps shared response helpers and the public
  `handleShortenerRequest()` entry point until a later cleanup.

### 7.3 Router responsibilities

The router should:

1. hold an ordered list of route entries;
2. match incoming request target against path patterns;
3. strip query strings before path matching;
4. extract named path parameters such as `{slug}` and `{id}`;
5. select the first matching route in registration order;
6. invoke the route handler;
7. return a controlled fallback response when no route matches.

The router should not:

1. validate slug syntax;
2. parse JSON bodies;
3. perform storage lookups;
4. emit analytics events;
5. know redirect lifecycle rules;
6. log or record metrics directly;
7. own TLS or async I/O.

### 7.4 Path matching semantics

Path patterns use literal segments and named captures:

- `/api/v1/links/{slug}`
- `/api/v1/links/{slug}/stats`
- `/api/v1/links/id/{id}`
- `/r/{slug}`
- `/{slug}`

Matching algorithm:

1. Split the request target at `?`.
2. Match only the path portion.
3. Split path and pattern by `/`.
4. Empty leading segments from the initial slash are ignored.
5. Segment counts must match.
6. Literal pattern segments must match exactly and case-sensitively.
7. Capture segments must have the form `{name}`.
8. Captured values are stored in `RouteContext::path_params`.
9. The router does not URL-decode captured values.
10. Handler-level validation remains responsible for slug/id correctness.

Examples:

| Pattern | Target | Match | Captures |
|---|---|---:|---|
| `/api/v1/links/{slug}` | `/api/v1/links/docs` | yes | `slug=docs` |
| `/api/v1/links/{slug}/stats` | `/api/v1/links/docs/stats?bucket=hour` | yes | `slug=docs` |
| `/api/v1/links/id/{id}` | `/api/v1/links/id/abc` | yes | `id=abc` |
| `/api/v1/links/{slug}` | `/api/v1/links/docs/stats` | no | segment count differs |
| `/r/{slug}` | `/r/docs` | yes | `slug=docs` |
| `/{slug}` | `/docs` | yes | `slug=docs` |
| `/{slug}` | `/api/v1/links` | no | segment count differs |

### 7.5 Registration order

The router uses first-match-wins ordering. Registration order is therefore part
of the API contract.

Required order:

1. exact observability paths;
2. most-specific canonical API paths;
3. shorter canonical API paths;
4. compatibility API paths;
5. prefixed redirect path;
6. root redirect path;
7. generic fallback paths.

Important precedence rules:

- `/api/v1/links/id/{id}` must be before `/api/v1/links/{slug}`.
- `/api/v1/links/{slug}/stats` and other action routes must be before
  `/api/v1/links/{slug}`.
- `/r/{slug}` must be before `/{slug}`.
- Fallback `/{path}` must be last.

### 7.6 Handler boundaries

Handlers should be extracted by behavior group.

#### Observability handlers

Functions:

- `handleHealthz`
- `handleReadyz`
- `handleMetrics`

These should be very small wrappers around existing response helpers and
`renderMetrics()`.

#### Link management handlers

Functions:

- `handleCreateLink`
- `handleGetLinkById`
- `handleGetLinkBySlug`
- `handlePatchLink`
- `handleDeleteLink`
- `handlePreviewLink`
- `handlePlaceholderFeature`
- `handleLinkStats`
- `handleEnableLink`
- `handleDisableLink`
- `handleRestoreLink`

These handlers preserve existing parsing and validation:

- `extractJsonStringField`
- `extractJsonBoolField`
- `extractJsonStringArrayField`
- `extractJsonFlatObjectStringField`
- `extractJsonValueToken`
- `normalizeTargetUrl`
- `isValidSlug`
- `isReservedSlug`
- `parseRfc3339Zulu`
- `parseRedirectType`
- tag/metadata/campaign validators

#### Compatibility handlers

Functions:

- `handleCompatCreateLink`
- `handleCompatGetLinkBySlug`

Create can delegate to the canonical create implementation with compatibility
field handling for `code`.

#### Redirect handlers

Functions:

- `handlePrefixedRedirect`
- `handleRootRedirect`

These must remain narrow:

1. read slug from `RouteContext`;
2. validate slug;
3. read link through the existing fast-path helper;
4. evaluate link status;
5. apply password/rate-limit hooks;
6. update redirect counters;
7. emit click analytics;
8. return redirect or error.

No management API logic belongs here.

#### Fallback handlers

Functions:

- `handleFallbackUriStore`

This can call existing `handleApplicationRequest()` initially. A later cleanup
can move the URI store fallback implementation out of `request_handlers.cpp`.

## 8. Migration stages

### Stage R0: Baseline and characterization

Purpose: lock current behavior before moving dispatch.

Tasks:

1. Expand HTTP tests to cover every route in the endpoint inventory.
2. Cover wrong-method behavior for each group.
3. Cover key error shapes:
   - invalid URL;
   - invalid slug;
   - reserved slug;
   - slug conflict;
   - missing link;
   - disabled link;
   - expired link;
   - deleted link;
   - placeholder feature not enabled;
   - invalid stats query.
4. Cover request-id propagation on success and error responses.
5. Cover route labels for representative targets.
6. Cover root redirect fallthrough to `handleApplicationRequest()`.

Deliverables:

- new or expanded tests only;
- no dispatch changes.

Review gate:

- tests demonstrate current behavior clearly enough that later changes can be
  reviewed as mechanical behavior-preserving refactors.

### Stage R1: Router infrastructure

Purpose: add router mechanics without migrating real handlers.

Tasks:

1. Add `RouteContext`, `Router`, and `RouterBuilder`.
2. Implement path-pattern matching.
3. Implement method matching.
4. Add stub-handler unit tests for dispatch order and path captures.
5. Add validation that every dispatch route has a matching `RouteDescriptor`.
6. Keep `handleShortenerRequest()` dispatch unchanged.

Deliverables:

- router infrastructure;
- no production behavior change.

Review gate:

- `Router` is small, dependency-free, and does not import storage/core business
  logic.

### Stage R2: Observability routes

Purpose: migrate the safest exact routes.

Tasks:

1. Register `GET /healthz`, `GET /readyz`, and `GET /metrics`.
2. Extract observability handlers.
3. Route those requests through `Router::dispatch()`.
4. Keep all other requests on the old branch chain.

Deliverables:

- first production dispatch through router;
- small behavior-preserving diff.

Review gate:

- exact status/body/content-type behavior unchanged.

### Stage R3: Link management routes

Purpose: migrate canonical `/api/v1/links` management routes.

Sub-stages:

1. Read-only routes:
   - `GET /api/v1/links/id/{id}`;
   - `GET /api/v1/links/{slug}`;
   - `GET /api/v1/links/{slug}/preview`.
2. Stats and placeholders:
   - `GET /api/v1/links/{slug}/stats`;
   - `GET /api/v1/links/{slug}/qr`;
   - `GET /api/v1/links/{slug}/routing`.
3. Lifecycle action routes:
   - enable;
   - disable;
   - restore.
4. Mutating resource routes:
   - create;
   - patch;
   - delete.

Tasks:

1. Extract handler functions from existing branches.
2. Replace manual substring action parsing with `RouteContext` captures.
3. Preserve all validation and response code paths.
4. Keep compatibility endpoints unmigrated until Stage R4/R5.

Review gate:

- each sub-stage should be reviewable independently;
- tests prove canonical API behavior is unchanged.

### Stage R4: Compatibility routes

Purpose: migrate `/api/v1/short-urls` compatibility behavior.

Tasks:

1. Register `POST /api/v1/short-urls`.
2. Register `GET /api/v1/short-urls/{slug}`.
3. Reuse canonical create/read implementations where possible.
4. Preserve `code` request-body compatibility.

Review gate:

- existing clients using compatibility endpoints receive identical responses.

### Stage R5: Redirect routes

Purpose: migrate or partially wire redirect routes without broadening the fast
path.

Tasks:

1. Register `GET /r/{slug}` and `GET /{slug}`.
2. Extract redirect handlers with no management-plane dependencies.
3. Preserve analytics emission and counter updates.
4. Preserve `passwordGuardCheck` and `rateLimitGuardAllow` behavior.
5. Preserve root redirect fallthrough semantics.
6. Benchmark or at least inspect route matching cost.

Recommended approach:

- First keep redirect dispatch outside the general router but use the same
  handler functions.
- Only move redirect route matching into the router after tests and review prove
  no behavior or latency regression.

Review gate:

- redirect path remains minimal;
- no management API helper is introduced into redirect handlers.

### Stage R6: Fallback routes

Purpose: migrate generic URI store fallback behavior last.

Tasks:

1. Register fallback `GET`, `POST`, and `DELETE` behavior.
2. Keep `handleApplicationRequest()` as the implementation initially.
3. Preserve root path label `app` and non-root fallback label behavior.
4. Remove old final fallback branches only after all previous routes are routed.

Review gate:

- root and non-root fallback behavior remains intentionally documented.

### Stage R7: OpenAPI/API documentation metadata

Purpose: make route metadata useful for generated documentation.

Tasks:

1. Extend `RouteDescriptor` with optional documentation fields:
   - tags;
   - operation id;
   - path parameter descriptions;
   - query parameter descriptions;
   - request body description;
   - response code descriptions;
   - deprecation/compatibility marker.
2. Add route metadata tests:
   - every route has non-empty summary;
   - every capture has a documented parameter;
   - every route has at least one success response;
   - compatibility routes are marked as compatibility.
3. Decide whether output is:
   - generated Markdown;
   - generated OpenAPI JSON/YAML file;
   - runtime `GET /api/v1/openapi.json`.
4. Do not add a JSON dependency unless a follow-up decision approves it.

Review gate:

- generated documentation is clearly derived from C++ route metadata.

### Stage R8: Cleanup

Purpose: remove obsolete branch-chain code and reduce header coupling.

Tasks:

1. Delete migrated branches from `handleShortenerRequest()`.
2. Keep `handleShortenerRequest()` as the public entry point.
3. Move helper declarations out of `request_handlers.h` if they become
   handler-private.
4. Keep response helpers public only if tests or other modules need them.
5. Update README/API reference once implementation lands.

Review gate:

- no route has two active implementations.

## 9. Testing plan

### 9.1 Unit tests

Add or expand:

- `tests/unit/http/01_route_registry.cpp`
- `tests/unit/http/02_router_match.cpp`
- `tests/unit/http/03_router_dispatch.cpp`
- `tests/unit/http/04_observability_handlers.cpp`
- `tests/unit/http/05_link_handlers.cpp`
- `tests/unit/http/06_redirect_handlers.cpp`
- `tests/unit/http/07_compat_handlers.cpp`
- `tests/unit/http/08_fallback_handlers.cpp`

Required unit coverage:

1. exact path matching;
2. path parameter extraction;
3. query string stripping;
4. registration order;
5. method mismatch behavior;
6. no-route behavior;
7. registry/dispatcher consistency;
8. request-id propagation through response helpers;
9. canonical link create/read/update/delete;
10. compatibility create/read;
11. redirect status handling for active/disabled/expired/deleted links;
12. analytics emission stays best-effort.

### 9.2 Contract/integration tests

Use existing CTest labels:

- `unit` for router and handler tests;
- `contract` if storage-agnostic behavior is touched through link services;
- `integration` if end-to-end handler behavior depends on storage adapters.

Do not add new test frameworks.

### 9.3 Characterization tests

Before removing old branches, add tests that prove current behavior for:

- successful create with generated slug;
- successful create with custom slug;
- reserved slug create rejection;
- invalid/private target URL rejection;
- read by id;
- read by slug;
- patch each mutable field;
- soft delete;
- restore;
- preview active/disabled/expired/deleted;
- stats with and without query range;
- prefixed redirect;
- root redirect;
- root redirect miss/fallback;
- compatibility create/read.

## 10. Validation commands

Use repository-local CMake build directories, not CLion temporary directories.

Configure on Windows:

```powershell
mkdir cmake-build
Set-Location cmake-build
cmake .. -G "Visual Studio 17 2022" `
  -DCMAKE_TOOLCHAIN_FILE="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows
```

Build the main binary:

```powershell
cmake --build . --target url_shortener --config Debug
```

Build and run a focused test:

```powershell
cmake --build . --target 01_route_registry --config Debug
ctest --test-dir . -C Debug -R "^01_route_registry$" --output-on-failure
```

Run all unit tests:

```powershell
ctest --test-dir . -C Debug -L unit --output-on-failure
```

Run all registered categories:

```powershell
ctest --test-dir . -C Debug -L "unit|contract|integration|e2e" --output-on-failure
```

Note: native Windows does not run POSIX e2e scripts unless the needed shell and
runtime facilities are available. CI remains the authoritative place for Linux
e2e validation.

## 11. Risk analysis

| Risk | Impact | Mitigation |
|---|---|---|
| Route precedence regression | Requests hit wrong handler. | First-match tests, route-order tests, staged migration. |
| Method mismatch behavior changes | Clients see different error status/body. | Characterization tests before migration. |
| Redirect fast path slowdown | Increased redirect latency. | Keep redirect matching simple, migrate late, review separately. |
| Root redirect/fallback semantics change | Generic URI store behavior breaks. | Explicit tests for `/{slug}` miss/fallback and `/`. |
| Analytics emission regression | Stats become inaccurate. | Redirect handler tests for emitted status codes and counters. |
| Documentation metadata drift | Generated docs become inaccurate. | Registry/Router consistency tests. |
| Header coupling increases | More files depend on large request handler header. | Introduce narrow handler/router headers. |
| Unclear ownership of helpers | Duplicate serialization/validation. | Move shared helpers only when reused; avoid premature extraction. |

## 12. Rollback plan

Each stage should be independently revertible.

Rollback strategy:

1. Keep old branch-chain dispatch until a route group is fully migrated and
   tested.
2. Migrate route groups behind small functions so reverting a group is a
   limited diff.
3. Avoid deleting old code in the same commit that introduces new router
   infrastructure.
4. If a production regression appears, restore the previous branch for that
   route group and keep the router infrastructure dormant.
5. Do not migrate redirect routes until all management routes are stable.

## 13. Review gates

Required review gates:

1. **Architecture review** before implementing `Router` and `RouterBuilder`.
2. **Feature review** after each route group migration.
3. **Security review** before redirect-route changes and before any change that
   touches URL normalization, slug validation, request body parsing, or
   untrusted redirect targets.
4. **Docs review** before generated API documentation becomes authoritative.
5. **Test review** after characterization coverage is added.

Suggested sequence:

```text
Plan review
  -> R0 characterization tests
  -> R1 router infrastructure
  -> R2 observability routes
  -> R3 link management routes
  -> R4 compatibility routes
  -> R5 redirect routes
  -> R6 fallback routes
  -> R7 documentation metadata
  -> R8 cleanup
```

## 14. Success criteria

The refactor is successful when:

1. every endpoint in the inventory is represented in `registeredRoutes()`;
2. every dispatch route has a corresponding `RouteDescriptor`;
3. every `RouteDescriptor` has a matching dispatch route;
4. `handleShortenerRequest()` no longer contains route-specific branch logic;
5. route handlers are named and testable independently;
6. the redirect fast path remains narrow and behavior-compatible;
7. all route labels remain stable;
8. compatibility endpoints continue to work;
9. unit tests cover router matching, dispatch ordering, and handler behavior;
10. the main binary builds with CMake/vcpkg on Windows;
11. focused HTTP/router CTest targets pass;
12. generated or maintained API docs can be derived from route metadata.

## 15. Acceptance checklist

Before implementation starts:

- [ ] Review and approve this plan.
- [ ] Confirm whether method mismatches should preserve current `400` behavior
      or intentionally move to `405 Method Not Allowed` in a later stage.
- [ ] Confirm whether OpenAPI output should be a generated file, a runtime
      endpoint, or both.
- [ ] Confirm whether fallback URI store routes should remain long-term.

Before merging router infrastructure:

- [ ] `Router` has no storage/business dependencies.
- [ ] path matching is covered by unit tests.
- [ ] route order is covered by unit tests.
- [ ] registry/router consistency is covered by unit tests.

Before migrating management routes:

- [ ] characterization tests exist for all canonical link endpoints.
- [ ] error response bodies are asserted, not just status codes.
- [ ] request-id header behavior is asserted.

Before migrating redirect routes:

- [ ] active, disabled, expired, deleted, missing, and invalid slug cases are
      covered.
- [ ] analytics event emission and status codes are covered.
- [ ] root redirect fallback behavior is covered.
- [ ] security review has no blocking findings.

Before enabling generated documentation:

- [ ] every route has summary metadata.
- [ ] every path parameter has description metadata.
- [ ] every query parameter has description metadata where applicable.
- [ ] compatibility routes are marked clearly.
- [ ] placeholder routes are marked as not implemented.

## 16. Open questions for review

1. Should wrong-method responses remain `400 invalid_method` for strict backward
   compatibility, or should the router introduce `405 Method Not Allowed` with
   an `Allow` header?
2. Should `/api/v1/openapi.json` be exposed at runtime, generated as a checked-in
   artifact, or omitted until a separate documentation stage?
3. Should the generic URI store fallback remain part of the production API, or
   should this refactor prepare a deprecation path?
4. Should route metadata include full response schemas now, or only summaries
   and parameters until a JSON/OpenAPI writer is approved?
5. Should redirect route matching stay outside the generic router permanently
   for performance clarity, while still using extracted redirect handlers?

## 17. Reviewer notes

Review should focus on:

- whether the route grouping and migration order are safe;
- whether redirect-path protections are strong enough;
- whether the proposed router is intentionally small enough;
- whether the acceptance criteria are sufficient to prevent behavior drift;
- whether any endpoint is missing from the inventory;
- whether OpenAPI generation should be runtime or build-time.

No product code changes should be made from this document until the plan is
reviewed and accepted.
