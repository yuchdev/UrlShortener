# 02 - Extract compatibility read handler

**Parent task:** 05.0 Compatibility routes
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 03

## Objective

Move `/api/v1/short-urls/{slug}` read behavior into a named handler.

## Files to modify

- `include/url_shortener/http/handlers/CompatibilityHandlers.hpp`
- `src/http/handlers/CompatibilityHandlers.cpp`
- `tests/unit/http/11_compat_read_handler.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/compatibility_handlers.hpp` and
`src/http/handlers/compatibility_handlers.cpp` (renamed to snake_case by
commit `3f4c170 Rename files to snake_style`). The proposed test file was
consolidated with the Task 05.0 subtask 01 test file into a single
`tests/unit/http/11_compatibility_handlers.cpp` - see the Task 05.0
[README.md](README.md) implementation note.

## New function

```cpp
BeastResponse handleCompatGetLinkBySlug(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    const RouteContext& context);
```

## Existing logic to move

From `handleShortenerRequest()`:

- branch matching `shortener_api_compat_prefix + '/'`;
- slug extraction;
- `getLinkForRead(slug)`;
- `serializeLink(*link, makeShortUrl(...))`;
- missing link error.

## Tests

Add tests for:

- read existing compatibility slug;
- read missing slug;
- wrong method behavior through router dispatch;
- route context missing `slug` fails safely with current not-found shape.

## Constraints

- Canonical read handler is not duplicated unless necessary.
- Compatibility response remains equivalent to current behavior.

## Success criteria

- [x] `include/url_shortener/http/handlers/compatibility_handlers.hpp`
      declares `handleCompatGetLinkBySlug`.
- [x] `tests/unit/http/11_compatibility_handlers.cpp` covers all four listed
      read cases.
- [x] The canonical read handler is not duplicated unless necessary.
- [x] Compatibility response remains equivalent to current behavior.
