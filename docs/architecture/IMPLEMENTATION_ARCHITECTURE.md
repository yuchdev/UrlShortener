# Implementation Architecture Specification

## 1. Purpose

This document defines the **target implementation architecture** for the low-latency URL shortener and supplements the stage specifications with stable architectural rules for autonomous implementation.

The goal is to prevent architectural drift while the system evolves from the current single-binary HTTP/HTTPS service into a production-grade shortener with multiple storage backends, bounded analytics overhead, and operational hardening.

This document is normative for implementation decisions unless a later stage specification explicitly overrides a detail.

---

## 2. Primary architectural goals

The implementation must optimize for the following goals, in descending order of importance:

1. **Correct redirect behavior** for active, expired, disabled, deleted, and invalid links.
2. **Low and predictable redirect latency** under normal and degraded operating conditions.
3. **Operational simplicity** for development, debugging, and production rollout.
4. **Testability** through clear seams between domain logic, transport, persistence, cache, and analytics.
5. **Controlled extensibility** for storage backends, rate limits, analytics sinks, and future management features.
6. **Security-by-default** for input validation, target URL policy, and secret handling.

The system is not required to become a general-purpose web framework or plugin platform.

---

## 3. Architecture principles

### 3.1 Control plane vs data plane

The implementation must maintain a hard conceptual split between:

- **Data plane**: redirect serving path used by public clients.
- **Control plane**: management APIs used to create, inspect, update, disable, delete, restore, preview, and inspect stats.

The data plane must remain intentionally narrow and highly optimized.
The control plane may be richer and more flexible, but it must not introduce complexity into redirect resolution.

### 3.2 Domain-first design

The `Link` domain model and redirect/lifecycle rules are core business logic.
They must not depend on HTTP framework details, concrete database clients, or concrete cache implementations.

### 3.3 Ports and adapters

Persistence, cache, rate limiting, and analytics storage/sinks must be introduced behind interfaces.
Core services depend on interfaces only.
Backend implementations depend on concrete libraries.

### 3.4 Explicit fast path protection

The redirect path is the product.
Any code added to the redirect path must justify its latency cost.
Optional and future features must default to the control plane unless explicitly approved for data-plane use.

### 3.5 Measured, not assumed, performance

Performance-sensitive decisions must be benchmarked and documented.
No abstraction is “cheap enough” by assumption.

---

## 4. Target module layout

The exact file names may vary slightly, but the implementation must preserve the following logical structure.

```text
src/
  app/
    AppBuilder.*
    DependencyContainer.*
    Startup.*

  core/
    Link.*
    LinkId.*
    LinkState.*
    RedirectDecision.*
    Errors.*

  service/
    LinkService.*
    RedirectService.*
    PreviewService.*
    StatsService.*
    UrlValidationService.*
    SlugGenerator.*

  repository/
    ILinkRepository.*
    InMemoryLinkRepository.*
    SqliteLinkRepository.*
    PostgresLinkRepository.*

  cache/
    ILinkCache.*
    RedisLinkCache.*
    NullLinkCache.*

  analytics/
    ClickEvent.*
    IClickEventRepository.*
    IAnalyticsSink.*
    EventQueue.*
    AnalyticsWorker.*

  security/
    UrlPolicy.*
    RateLimitPolicy.*
    SecretProvider.*

  http/
    Router.*
    RequestContext.*
    ErrorEnvelope.*
    ApiHandlers.*
    RedirectHandler.*
    JsonCodec.*

  infra/
    Config.*
    Logging.*
    Metrics.*
    Clock.*
    ThreadPool.*
    TlsContextManager.*

  tests/
    unit/
    integration/
    contract/
    perf/
```

This structure is descriptive, not prescriptive at the directory-name level, but the dependency boundaries are mandatory.

---

## 5. Dependency rules

### 5.1 Allowed dependency direction

The dependency direction must be:

```text
HTTP / transport layer
    -> service layer
    -> repository/cache/analytics interfaces
    -> backend implementations
```

Allowed additional dependencies:

- service layer -> core
- repository implementations -> core
- cache implementations -> core DTOs used for cache records
- analytics worker -> analytics repository/sink interfaces
- infra -> low-level libraries

### 5.2 Forbidden dependency direction

The following are forbidden:

- core -> HTTP types
- core -> SQL client code
- core -> Redis client code
- service -> concrete DB driver types
- service -> concrete Redis client types
- service -> TLS/OpenSSL details
- HTTP handlers containing business rules that duplicate service logic
- backend implementations calling HTTP handlers

### 5.3 Serialization rule

JSON serialization/deserialization belongs in the HTTP boundary or persistence adapters.
It must not become the in-memory domain representation of links on the hot path.

---

## 6. Core domain model expectations

The core domain must expose a stable `Link` model with fields required by stage specifications.
At minimum, the implementation must support:

- internal identifier
- slug
- target URL
- redirect type
- enabled flag
- created/updated timestamps
- optional expiration timestamp
- optional deletion timestamp or equivalent soft-delete state
- optional metadata used by the control plane

The model may be split into:

- `RedirectLinkView` or equivalent hot-path read model
- `ManagedLink` or equivalent richer management model

This split is encouraged when it simplifies redirect latency and cache design.

---

## 7. Service layer responsibilities

### 7.1 LinkService

Responsible for:

- create link
- retrieve by id/slug
- update mutable management fields
- enable/disable
- soft delete / restore
- enforce slug uniqueness
- enforce URL validation policy
- apply lifecycle precedence rules

### 7.2 RedirectService

Responsible for:

- resolve slug
- determine effective link state
- return redirect decision / terminal error
- optionally enqueue analytics event without blocking redirect

### 7.3 PreviewService

Responsible for generating a non-redirect response describing what would happen for a given slug.
This service must reuse domain/state evaluation logic rather than re-implement it.

### 7.4 StatsService

Responsible for management-plane stats reads.
It must not be used from the redirect hot path.

---

## 8. HTTP layer responsibilities

HTTP handlers must be thin.
They may:

- parse request parameters/body
- validate transport-level constraints (content type, body size, malformed JSON)
- build request context including request ID
- call service methods
- translate service results into HTTP responses

HTTP handlers must not:

- contain business rules about link lifecycle precedence
- manually construct SQL queries
- contain caching logic beyond delegating to services
- block on analytics persistence if the redirect path is expected to proceed

---

## 9. Request lifecycle model

### 9.1 Create link request

1. HTTP handler parses request body.
2. `UrlValidationService` validates and normalizes destination.
3. `LinkService` checks slug rules and uniqueness.
4. Repository persists link.
5. Cache warming is optional and must not be required for correctness.
6. Response is returned with stable JSON envelope.

### 9.2 Redirect request

1. Redirect handler extracts slug.
2. `RedirectService` resolves slug via cache/repository strategy.
3. Service evaluates link state.
4. On active link, redirect response is returned.
5. Analytics event is enqueued best-effort only.
6. Redirect completion does not wait for analytics persistence.

### 9.3 Update/delete/restore request

1. HTTP handler validates request.
2. `LinkService` applies mutation.
3. Repository persists mutation.
4. Cache entry is invalidated or deterministically updated.
5. Response is returned.

---

## 10. Concurrency model

The implementation may use asynchronous I/O and worker threads, but the following rules apply:

- no unsynchronized mutable shared state
- repository implementations must document their thread-safety assumptions
- in-memory backends must use explicit synchronization
- analytics queue must be bounded
- cache access patterns must be safe under concurrent load
- lifecycle and uniqueness checks must preserve correctness under concurrent create/update workloads

If stronger transactional guarantees depend on the backend, service-level documentation must explain the difference.

---

## 11. Error model

The implementation must define a stable internal error taxonomy separate from HTTP status mapping.
Examples include:

- validation error
- slug conflict
- link not found
- link inactive
- malformed request
- backend unavailable
- timeout
- configuration error
- internal invariant violation

HTTP translation should happen at the boundary.
The same domain/service error must not map to multiple statuses without documented reason.

---

## 12. Configuration model

The architecture must support configuration-driven assembly of:

- HTTP listener(s)
- HTTPS listener(s)
- TLS settings
- metadata backend
- cache backend
- analytics backend/pipeline options
- logging configuration
- metrics configuration
- queue and pool sizing

Configuration parsing belongs in startup/bootstrap code, not domain services.

---

## 13. Extensibility rules

New features are allowed only if they fit one of these categories:

- control-plane API expansion
- backend adapter addition
- observability enhancement
- bounded analytics enhancement
- security policy tightening

Any future feature that touches the redirect path must document:

- latency impact
- allocation impact
- fallback behavior on dependency failure
- test plan

---

## 14. Documentation expectations

Implementation PRs must keep the architecture documents aligned with code changes when:

- a new core interface is introduced
- a module boundary changes
- a backend capability changes
- a new redirect-path dependency is introduced

Silent architectural drift is not acceptable.

---

## 15. Non-goals

The architecture is intentionally not trying to provide:

- generic arbitrary plugin execution in request path
- full multi-tenant SaaS control plane in the first implementation wave
- distributed transactions across metadata and analytics stores
- strongly consistent globally replicated redirects
- analytics-rich dashboards in the core service

These can be added later only after the low-latency core remains protected.
