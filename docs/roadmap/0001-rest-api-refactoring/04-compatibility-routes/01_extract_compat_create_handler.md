# Task 04.01 - Extract compatibility create handler

## Objective

Make `/api/v1/short-urls` create behavior explicit while preserving support for
the legacy `code` field.

## Files to add

- `include/url_shortener/http/handlers/CompatibilityHandlers.hpp`
- `src/http/handlers/CompatibilityHandlers.cpp`
- `tests/unit/http/10_compat_create_handler.cpp`

## Files to modify

- `sources.cmake`
- `CMakeLists.txt`

## New function

```cpp
BeastResponse handleCompatCreateLink(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    const RouteContext& context);
```

## Existing logic to move or reuse

The current create branch handles both:

- canonical `slug`;
- compatibility `code`.

Refactor so shared create behavior lives in a helper such as:

```cpp
BeastResponse createLinkFromRequest(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    CreateSlugFieldMode slug_mode);
```

`CreateSlugFieldMode` can be private to `CompatibilityHandlers.cpp` or
`LinkHandlers.cpp` until a better shared location is needed.

## Tests

Add tests for:

- create with `code`;
- create with generated slug;
- invalid `code`;
- reserved `code`;
- duplicate `code`;
- response shape matches canonical create.

## Acceptance criteria

- Existing `/api/v1/links` create tests still pass.
- No response field is renamed from the compatibility endpoint.

