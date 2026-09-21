# 04 - Register and switch canonical link routes

**Parent task:** 04.0 Link management routes
**State:** ✅ Complete
**Depends on:** 01, 02, 03
**Blocks:** none

## Objective

Route canonical `/api/v1/links` endpoints through `Router::dispatch()`.

## Files to modify

- `src/http/RouterBuilder.cpp`
- `src/http/request_handlers.cpp`
- `tests/unit/http/02_endpoint_matrix_characterization.cpp`
- `tests/unit/http/03_method_and_error_characterization.cpp`
- `tests/unit/http/05_router_registry_consistency.cpp`

Implementation note: shipped as `src/http/router_builder.cpp` (renamed to
snake_case by commit `3f4c170 Rename files to snake_style`) and
`tests/unit/http/08_router_registry_consistency.cpp` (renumbered to follow
the router-infrastructure test files, see Task 02.0). The other two test
files match their originally proposed names.

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

- Run all Task 01.0 characterization tests.
- Run handler tests from Tasks 04.0 subtasks 01-03.
- Run registry/router consistency tests.

## Constraints

- Canonical API behavior is unchanged.
- No compatibility endpoint is changed in this task.

## Success criteria

- [x] `src/http/router_builder.cpp` registers all twelve canonical
      `/api/v1/links` routes listed above, in the required precedence order
      (id lookup and action routes before the generic `{slug}` route).
- [x] `handleShortenerRequest()` dispatches `/api/v1/links` targets through
      the router; the old canonical branch was removed once characterization
      tests passed.
- [x] `/api/v1/short-urls`, redirects, and fallback remained on the old code
      path at the end of this task (migrated in Tasks 05.0-07.0).
- [x] `tests/unit/http/08_router_registry_consistency.cpp` passes with the
      full canonical route set registered.
- [x] Canonical API behavior is unchanged (Task 01.0 characterization suite
      passes unmodified).
- [x] No compatibility endpoint was changed in this task.
