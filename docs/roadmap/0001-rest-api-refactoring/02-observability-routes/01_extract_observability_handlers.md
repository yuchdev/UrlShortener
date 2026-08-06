# Task 02.01 - Extract health, readiness, and metrics handlers

## Objective

Extract the simplest exact-match handlers from `handleShortenerRequest()` into
named functions.

## Files to add

- `include/url_shortener/http/handlers/ObservabilityHandlers.hpp`
- `src/http/handlers/ObservabilityHandlers.cpp`
- `tests/unit/http/06_observability_handlers.cpp`

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

## Acceptance criteria

- Handler functions contain no path parsing.
- `handleShortenerRequest()` still owns production dispatch after this task.

