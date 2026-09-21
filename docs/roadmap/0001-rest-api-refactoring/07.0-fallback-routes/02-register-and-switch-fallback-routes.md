# 02 - Register and switch fallback routes

**Parent task:** 07.0 Fallback routes
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 03

## Objective

Route generic URI-store fallback behavior after all more-specific routes have
been migrated.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/14_fallback_handler.cpp`
- `tests/unit/http/04_redirect_fallback_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

Implementation note: shipped as `src/http/router_builder.cpp` (renamed to
snake_case by commit `3f4c170 Rename files to snake_style`) and
`tests/unit/http/08_router_registry_consistency.cpp` (renumbered to follow
the router-infrastructure test files, see Task 02.0). The other two test
files match their originally proposed names.

## Routes to register

```text
GET    /{path}
POST   /{path}
DELETE /{path}
GET    /
POST   /
DELETE /
```

## Ordering requirements

Fallback routes must be registered after:

- observability routes;
- canonical API routes;
- compatibility API routes;
- `/r/{slug}`;
- `/{slug}` root redirect.

## Dispatch switch

In `handleShortenerRequest()`:

- use router dispatch for any target not already handled by earlier migrated
  groups;
- delete old final `return handleApplicationRequest(...)` only after tests pass.

## Tests

- fallback handler tests;
- root redirect fallback tests;
- endpoint matrix tests;
- route order tests proving fallback does not steal API or redirect targets.

## Constraints

- No route has two active production implementations.
- Fallback remains last.

## Success criteria

- [x] `src/http/router_builder.cpp` registers `/{path}` (`GET`/`POST`/
      `DELETE`) and `/` (`GET`/`POST`/`DELETE`) after every observability,
      canonical API, compatibility API, and redirect route (verified: they
      are the final six entries in `buildApplicationRouter()`).
- [x] `handleShortenerRequest()` dispatches all remaining targets through the
      router; the old final `handleApplicationRequest(...)` fallthrough was
      removed once tests passed.
- [x] `tests/unit/http/14_fallback_handler.cpp`,
      `tests/unit/http/04_redirect_fallback_characterization.cpp`, and
      `tests/unit/http/08_router_registry_consistency.cpp` all pass.
- [x] No route has two active production implementations.
- [x] Fallback remains registered last.
