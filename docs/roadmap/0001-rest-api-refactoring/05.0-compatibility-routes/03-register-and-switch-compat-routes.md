# 03 - Register and switch compatibility routes

**Parent task:** 05.0 Compatibility routes
**State:** ✅ Complete
**Depends on:** 01, 02
**Blocks:** none

## Objective

Route `/api/v1/short-urls` endpoints through `Router::dispatch()`.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/02_endpoint_matrix_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

Implementation note: shipped as `src/http/router_builder.cpp` (renamed to
snake_case by commit `3f4c170 Rename files to snake_style`) and
`tests/unit/http/08_router_registry_consistency.cpp` (renumbered to follow
the router-infrastructure test files, see Task 02.0).

## Routes to register

```text
POST /api/v1/short-urls
GET  /api/v1/short-urls/{slug}
```

## Dispatch switch

In `handleShortenerRequest()`:

- route targets that start with `/api/v1/short-urls` through the router;
- remove the old compatibility branch after tests pass;
- keep redirect and fallback branches unchanged.

## Tests

- compatibility create/read tests;
- full endpoint matrix tests;
- registry/router consistency tests.

## Constraints

- Both canonical and compatibility APIs are router-backed.
- Redirect fast path remains untouched.

## Success criteria

- [x] `src/http/router_builder.cpp` registers
      `POST /api/v1/short-urls` and `GET /api/v1/short-urls/{slug}`.
- [x] `handleShortenerRequest()` dispatches `/api/v1/short-urls` targets
      through the router; the old compatibility branch was removed once
      tests passed.
- [x] `tests/unit/http/02_endpoint_matrix_characterization.cpp` and
      `tests/unit/http/08_router_registry_consistency.cpp` pass with both
      canonical and compatibility routes registered.
- [x] Redirect and fallback branches remained unchanged by this task.
