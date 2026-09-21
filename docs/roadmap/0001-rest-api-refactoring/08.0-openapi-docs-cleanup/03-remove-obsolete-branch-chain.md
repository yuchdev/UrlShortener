# 03 - Remove obsolete branch-chain dispatch

**Parent task:** 08.0 OpenAPI/docs cleanup
**State:** ✅ Complete
**Depends on:** 01, 02
**Blocks:** 04

## Objective

After every route group is router-backed, remove old route-specific branches
from `handleShortenerRequest()`.

## Files to modify

- `src/http/request_handlers.cpp`
- `include/url_shortener/http/request_handlers.h`
- `sources.cmake`
- `CMakeLists.txt`

Implementation note: these paths match their originally proposed names - no
renames were needed for this subtask.

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

## Constraints

- No route has a duplicate old and new implementation.
- `request_handlers.cpp` remains buildable and simpler.

## Success criteria

- [x] `handleShortenerRequest()` in `src/http/request_handlers.cpp` matches
      the required final shape exactly: a one-line delegation to
      `applicationRouter().dispatch(req, config, is_tls)` (verified directly
      against the source, lines 465-471).
- [x] No route-specific branching remains in `handleShortenerRequest()`.
- [x] Full unit HTTP test suite passes: 148/148 (`ctest -L unit`); the
      HTTP/router subset is 29/29 (`ctest -R "^0[1-9]_|^1[0-4]_" -L unit`).
- [x] The main binary (`url_shortener`) builds cleanly against the current
      `cmake-build/` build.
- [x] No route has a duplicate old and new implementation.
- [x] `request_handlers.cpp` remains buildable and simpler.
