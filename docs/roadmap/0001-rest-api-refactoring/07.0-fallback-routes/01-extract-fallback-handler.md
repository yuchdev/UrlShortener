# 01 - Extract fallback URI-store handler wrapper

**Parent task:** 07.0 Fallback routes
**State:** ✅ Complete
**Depends on:** none (depends on Task 06.0 being complete)
**Blocks:** 02

## Objective

Wrap `handleApplicationRequest()` as a named router handler.

## Files to add

- `include/url_shortener/http/handlers/FallbackHandlers.hpp`
- `src/http/handlers/FallbackHandlers.cpp`
- `tests/unit/http/14_fallback_handler.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/fallback_handlers.hpp` and
`src/http/handlers/fallback_handlers.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`); the test file matches the originally
proposed name and number.

## Files to modify

- `sources.cmake`
- `CMakeLists.txt`

## New function

```cpp
BeastResponse handleFallbackUriStore(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    const RouteContext& context);
```

## Implementation

Initial implementation should be a thin wrapper:

```cpp
return url_shortener::handleApplicationRequest(req, config, is_tls);
```

Do not move `UriMapSingleton` logic yet.

## Tests

Add tests for:

- `POST /fallback-doc` stores content;
- `GET /fallback-doc` returns content;
- `DELETE /fallback-doc` deletes content;
- unsupported method returns current fallback behavior.

## Constraints

- No behavior change.
- This task only creates a seam for later routing.

## Success criteria

- [x] `include/url_shortener/http/handlers/fallback_handlers.hpp` declares
      `handleFallbackUriStore`.
- [x] `tests/unit/http/14_fallback_handler.cpp` covers store/read/delete and
      unsupported-method cases.
- [x] No behavior change - the handler wraps `handleApplicationRequest()`
      (confirmed as the final, permanent shape in subtask 03, Option A).
- [x] This task created a router seam only; routing was switched in
      subtask 02.
