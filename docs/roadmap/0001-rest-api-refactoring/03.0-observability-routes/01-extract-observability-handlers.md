# 01 - Extract health, readiness, and metrics handlers

**Parent task:** 03.0 Observability routes
**State:** ✅ Complete
**Depends on:** none (depends on Task 02.0 being complete)
**Blocks:** 02

## Objective

Extract the simplest exact-match handlers from `handleShortenerRequest()` into
named functions.

## Files to add

- `include/url_shortener/http/handlers/ObservabilityHandlers.hpp`
- `src/http/handlers/ObservabilityHandlers.cpp`
- `tests/unit/http/06_observability_handlers.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/observability_handlers.hpp`,
`src/http/handlers/observability_handlers.cpp`, and
`tests/unit/http/09_observability_handlers.cpp` (renamed to snake_case by
commit `3f4c170 Rename files to snake_style`; test renumbered to follow the
router-infrastructure test files).

## Files to modify

- `sources.cmake`
- `CMakeLists.txt`

## New functions

In namespace `url_shortener::http`:

```cpp
BeastResponse handleHealthz(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

BeastResponse handleReadyz(...);
BeastResponse handleMetrics(...);
```

## Behavior

- `handleHealthz` returns `makeResponse(..., 200, "ok\n", "text/plain")`.
- `handleReadyz` returns the same body and content type.
- `handleMetrics` returns `makeResponse(..., 200, renderMetrics(), "text/plain")`.
- Method validation remains in router/registration for this stage; if these
  functions are called directly in tests with the wrong method, they may still
  return the success response.

## Tests

Test direct handler calls:

- status code;
- body;
- content type;
- `X-Request-Id` response header.

## Constraints

- Handler functions contain no path parsing.
- `handleShortenerRequest()` still owns production dispatch after this task.

## Success criteria

- [x] `include/url_shortener/http/handlers/observability_handlers.hpp`
      declares `handleHealthz`, `handleReadyz`, and `handleMetrics` with the
      signatures shown above.
- [x] `tests/unit/http/09_observability_handlers.cpp` asserts status code,
      body, content type, and `X-Request-Id` for all three handlers.
- [x] Handler functions contain no path parsing.
- [x] `handleShortenerRequest()` still owned production dispatch at the end
      of this subtask (switched in subtask 03 of this same task).
