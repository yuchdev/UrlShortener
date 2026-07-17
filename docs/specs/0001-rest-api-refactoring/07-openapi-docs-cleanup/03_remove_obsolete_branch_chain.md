# Task 07.03 - Remove obsolete branch-chain dispatch

## Objective

After every route group is router-backed, remove old route-specific branches
from `handleShortenerRequest()`.

## Files to modify

- `src/http/request_handlers.cpp`
- `include/url_shortener/http/request_handlers.h`
- `sources.cmake`
- `CMakeLists.txt`

## Required final shape

`handleShortenerRequest()` should become a thin entry point:

```cpp
bhttp::response<bhttp::string_body> handleShortenerRequest(
    const bhttp::request<bhttp::string_body>& req,
    const ServerConfig& config,
    const bool is_tls)
{
    return applicationRouter().dispatch(req, config, is_tls);
}
```

The exact helper name may differ, but route-specific branching should no longer
live in this function.

## Cleanup rules

- Keep response helpers such as `makeResponse()` and `makeApiErrorResponse()`
  if handlers use them.
- Remove now-unused constants from `request_handlers.cpp`.
- Move helper functions to narrower files only when that reduces includes and
  does not create duplication.
- Do not delete TLS helper functions from `request_handlers.cpp` unless their
  callers are moved too.

## Tests

- all unit HTTP tests;
- all endpoint characterization tests;
- focused redirect tests;
- route registry consistency tests;
- build `url_shortener`.

## Acceptance criteria

- No route has a duplicate old and new implementation.
- `request_handlers.cpp` remains buildable and simpler.

