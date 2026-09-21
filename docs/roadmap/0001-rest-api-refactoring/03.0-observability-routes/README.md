# Task 03.0 - Observability routes

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Migrate the safest, exact-match routes first: `GET /healthz`, `GET /readyz`,
and `GET /metrics`. This is the first production use of `Router::dispatch()`
inside `handleShortenerRequest()`, proving the router infrastructure from
Task 02.0 end-to-end on low-risk routes before any `/api` or redirect
behavior is touched. All other requests remain on the old branch chain until
later tasks.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Extract health, readiness, and metrics handlers](01-extract-observability-handlers.md) | ✅ Complete | 02 |
| 02 | [Register observability handlers in RouterBuilder](02-register-observability-routes.md) | ✅ Complete | 03 |
| 03 | [Route observability requests through Router](03-switch-observability-dispatch.md) | ✅ Complete | none |

## Key constraints

- Handler functions contain no path parsing.
- `handleShortenerRequest()` still owns production dispatch for every other
  route group after this task.
- No `/api`, redirect, or fallback request is routed in this task.
- Registry/router consistency tests must pass once handlers are registered.
