# Task 03.04 - Register and switch canonical link routes

## Objective

Route canonical `/api/v1/links` endpoints through `Router::dispatch()`.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/02_endpoint_matrix_characterization.cpp`
- `tests/unit/http/03_method_and_error_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

## Routes to register

```text
POST   /api/v1/links
GET    /api/v1/links/id/{id}
GET    /api/v1/links/{slug}
PATCH  /api/v1/links/{slug}
DELETE /api/v1/links/{slug}
GET    /api/v1/links/{slug}/preview
GET    /api/v1/links/{slug}/qr
GET    /api/v1/links/{slug}/routing
GET    /api/v1/links/{slug}/stats
POST   /api/v1/links/{slug}/enable
POST   /api/v1/links/{slug}/disable
POST   /api/v1/links/{slug}/restore
```

## Dispatch switch

In `handleShortenerRequest()`:

- route targets that start with `/api/v1/links` through the router;
- keep `/api/v1/short-urls`, redirects, and fallback on the old code until
  later stages;
- remove the old canonical branch only after characterization tests pass.

## Tests

- Run all Stage 00 characterization tests.
- Run handler tests from Tasks 03.01 through 03.03.
- Run registry/router consistency tests.

## Acceptance criteria

- Canonical API behavior is unchanged.
- No compatibility endpoint is changed in this task.

