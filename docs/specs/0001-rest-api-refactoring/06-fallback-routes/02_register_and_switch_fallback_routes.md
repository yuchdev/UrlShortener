# Task 06.02 - Register and switch fallback routes

## Objective

Route generic URI-store fallback behavior after all more-specific routes have
been migrated.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/14_fallback_handler.cpp`
- `tests/unit/http/04_redirect_fallback_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

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

## Acceptance criteria

- No route has two active production implementations.
- Fallback remains last.

