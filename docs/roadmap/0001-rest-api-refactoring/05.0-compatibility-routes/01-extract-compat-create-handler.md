# 01 - Extract compatibility create handler

**Parent task:** 05.0 Compatibility routes
**State:** ✅ Complete
**Depends on:** none (depends on Task 04.0 being complete)
**Blocks:** 03

## Objective

Make `/api/v1/short-urls` create behavior explicit while preserving support for
the legacy `code` field.

## Files to add

- `include/url_shortener/http/handlers/CompatibilityHandlers.hpp`
- `src/http/handlers/CompatibilityHandlers.cpp`
- `tests/unit/http/10_compat_create_handler.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/compatibility_handlers.hpp` and
`src/http/handlers/compatibility_handlers.cpp` (renamed to snake_case by
commit `3f4c170 Rename files to snake_style`). The proposed test file was
consolidated with the Task 05.0 subtask 02 test file into a single
`tests/unit/http/11_compatibility_handlers.cpp` - see the Task 05.0
[README.md](README.md) implementation note.

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

## Constraints

- Existing `/api/v1/links` create tests still pass.
- No response field is renamed from the compatibility endpoint.

## Success criteria

- [x] `include/url_shortener/http/handlers/compatibility_handlers.hpp`
      declares `handleCompatCreateLink`.
- [x] `tests/unit/http/11_compatibility_handlers.cpp` covers all six listed
      create cases.
- [x] Existing `/api/v1/links` create tests still pass.
- [x] No response field is renamed from the compatibility endpoint.
