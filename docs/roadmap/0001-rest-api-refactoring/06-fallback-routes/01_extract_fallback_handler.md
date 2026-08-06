# Task 06.01 - Extract fallback URI-store handler wrapper

## Objective

Wrap `handleApplicationRequest()` as a named router handler.

## Files to add

- `include/url_shortener/http/handlers/FallbackHandlers.hpp`
- `src/http/handlers/FallbackHandlers.cpp`
- `tests/unit/http/14_fallback_handler.cpp`

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

## Acceptance criteria

- No behavior change.
- This task only creates a seam for later routing.

