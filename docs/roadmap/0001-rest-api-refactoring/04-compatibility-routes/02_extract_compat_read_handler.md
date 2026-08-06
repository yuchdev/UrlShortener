# Task 04.02 - Extract compatibility read handler

## Objective

Move `/api/v1/short-urls/{slug}` read behavior into a named handler.

## Files to modify

- `include/url_shortener/http/handlers/CompatibilityHandlers.hpp`
- `src/http/handlers/CompatibilityHandlers.cpp`
- `tests/unit/http/11_compat_read_handler.cpp`

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

## Acceptance criteria

- Canonical read handler is not duplicated unless necessary.
- Compatibility response remains equivalent to current behavior.

