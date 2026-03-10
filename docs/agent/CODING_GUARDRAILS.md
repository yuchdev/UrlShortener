# Coding Guardrails for Autonomous Development

## 1. Purpose

This document defines mandatory implementation guardrails for coding agents and human contributors.
It exists to keep the codebase consistent, testable, performant, and reviewable while multiple stages are implemented.

These guardrails are intentionally explicit.
If a contribution conflicts with them, the contribution should be revised rather than rationalized post hoc.

---

## 2. Language and platform baseline

- Primary implementation language: **C++17** unless the repository baseline is formally upgraded.
- Core server stack: Boost.Asio + Boost.Beast + OpenSSL.
- The implementation should remain portable across standard Linux build environments used in CI.
- Do not introduce platform-specific behavior in core logic without a documented reason and fallback.

---

## 3. Architectural guardrails

### 3.1 Do not mix layers

Forbidden patterns include:

- SQL or Redis logic inside HTTP handlers
- business rules hard-coded in routing code
- domain objects depending on HTTP request/response classes
- analytics persistence logic embedded directly in redirect handler business flow

### 3.2 Keep redirect code narrow

Do not add management-only behavior to the redirect fast path.
If a feature does not directly affect redirect correctness, it probably belongs outside the redirect path.

### 3.3 No accidental framework creep

Do not turn the codebase into a general application framework.
Prefer focused interfaces and concrete implementations aligned with stage requirements.

---

## 4. State and concurrency guardrails

- Do not introduce unsynchronized global mutable state.
- Singleton usage is strongly discouraged unless already unavoidable and wrapped behind explicit interfaces.
- Thread safety assumptions must be documented near the type that owns shared state.
- Bounded queues must remain bounded; do not convert them into unbounded “temporary” buffers.
- Avoid hidden background threads without lifecycle ownership documented in startup/shutdown code.

---

## 5. Error-handling guardrails

- Do not swallow backend errors silently.
- Do not use exceptions for routine control flow across performance-sensitive paths unless existing project style already mandates it and the behavior is measured.
- Convert low-level errors into stable domain/service errors before HTTP mapping.
- Preserve `request_id` or equivalent correlation context in error logging where available.

---

## 6. Dependency guardrails

### 6.1 Approved dependency classes

Dependencies may be added only if they clearly fall into one of these groups:

- networking/runtime support
- structured logging
- JSON encoding/decoding
- SQL/PostgreSQL adapter support
- Redis adapter support
- testing framework/mocking support
- benchmarking support

### 6.2 Do not add dependencies casually

Forbidden without explicit justification:

- heavy web frameworks
- ORMs that obscure query behavior
- generic dependency injection frameworks
- reflection-heavy serialization frameworks without demonstrated benefit
- libraries that duplicate existing stack capabilities already present in the project

### 6.3 Dependency addition policy

Any new dependency must document:

- why existing stack is insufficient
- what module uses it
- how it affects build complexity
- any runtime/security implications

---

## 7. Data representation guardrails

- Do not store the main in-memory redirect representation as ad hoc JSON text.
- Prefer typed models and explicit DTOs.
- Keep cache records compact and redirect-oriented.
- Avoid stringly-typed enums where a narrow typed representation is feasible.
- Use explicit timestamp formats and conversion boundaries.

---

## 8. API guardrails

- Preserve stable route and payload contracts once introduced, unless a migration note explicitly allows a breaking change.
- Do not expose backend-specific semantics directly through API payloads.
- Error response shape should be consistent across endpoints.
- Preview/stats/admin endpoints must remain clearly separated from redirect endpoints.

---

## 9. Performance guardrails

- Do not block redirect completion on analytics persistence.
- Do not add expensive string parsing or JSON work to redirect logic without measurement.
- Do not perform redundant repository lookups when a single lookup can provide required state.
- Avoid allocating large temporary objects on every request.
- Any cache invalidation/update strategy must be deterministic and documented.

---

## 10. Security guardrails

- Use parameterized SQL only.
- Never hard-code secrets.
- Validate and normalize input at the service boundary.
- Reject disallowed URL schemes according to security policy.
- Do not log secrets or indiscriminately dump raw request bodies in production paths.

---

## 11. Testing guardrails

- Every new feature must include tests in the same PR.
- Every bug fix must include a regression test where feasible.
- Backend implementations must participate in contract tests where applicable.
- Do not merge code that only “works manually” without automated coverage.
- When behavior differs by backend, tests must document and justify the difference.

---

## 12. Documentation guardrails

A code change is incomplete if it alters behavior but does not update the relevant documentation.
At minimum, update docs when changing:

- route contracts
- configuration keys
- backend capabilities
- migration behavior
- benchmark/reporting expectations
- operational procedures

---

## 13. Review checklist for agents

Before considering a task complete, verify:

1. Are module boundaries respected?
2. Are tests added and passing?
3. Are docs updated?
4. Is redirect fast path still protected?
5. Are security/input limits respected?
6. Are new dependencies justified?
7. Is there any hidden global state or lifecycle ambiguity?

---

## 14. Explicit anti-patterns

The following anti-patterns are explicitly disallowed:

- “temporary” duplicated business logic in handlers
- backend-specific `if postgres` / `if sqlite` branches spread through service code
- silent fallback from validated behavior to permissive behavior on malformed input
- analytics worker that retries forever without bounds
- management metadata loaded into redirect path because it is “convenient”
- TODO-based pseudo-implementations marked complete without tests and docs

---

## 15. Completion rule

A task is not complete until:

- implementation is present
- automated tests are present
- documentation is updated
- build/CI expectations are satisfied
- no guardrail violation remains unaddressed
