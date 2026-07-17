# Task 04.03 - Register and switch compatibility routes

## Objective

Route `/api/v1/short-urls` endpoints through `Router::dispatch()`.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/02_endpoint_matrix_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

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

## Acceptance criteria

- Both canonical and compatibility APIs are router-backed.
- Redirect fast path remains untouched.

