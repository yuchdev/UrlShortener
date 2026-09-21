# Task 07.0 - Fallback routes

**Parent milestone:** [Milestone 0001 - REST API Refactoring](/docs/roadmap/0001-rest-api-refactoring/plan.md)
**Status:** ✅ Complete

## Scope

Route the generic URI-store fallback behavior (`GET`/`POST`/`DELETE` on
`/{path}` and `/`) through `Router` last, after every more-specific route
group (observability, canonical links, compatibility, redirects) is already
router-backed. Wrap `handleApplicationRequest()` as a named handler first,
then decide deliberately whether to keep it as the implementation or move its
`UriMapSingleton` logic into the new handler file.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Extract fallback URI-store handler wrapper](01-extract-fallback-handler.md) | ✅ Complete | 02 |
| 02 | [Register and switch fallback routes](02-register-and-switch-fallback-routes.md) | ✅ Complete | 03 |
| 03 | [Clarify handleApplicationRequest boundary](03-cleanup-application-request-boundary.md) | ✅ Complete | none |

## Key constraints

- Fallback routes are registered after observability, canonical API,
  compatibility API, `/r/{slug}`, and root `/{slug}` routes.
- No route has two active production implementations once this task
  completes.
- Whichever `handleApplicationRequest()` boundary is chosen, it is documented
  in code comments and no duplicated URI-store implementation remains.

## Implementation note

Option A was selected for the `handleApplicationRequest()` boundary (subtask
03): `handleApplicationRequest()` remains the implementation, and
`handleFallbackUriStore()` in `src/http/handlers/fallback_handlers.cpp` wraps
it rather than moving `UriMapSingleton` logic into the handler file.
