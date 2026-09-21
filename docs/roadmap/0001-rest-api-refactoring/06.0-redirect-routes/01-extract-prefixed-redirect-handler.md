# 01 - Extract prefixed redirect handler

**Parent task:** 06.0 Redirect routes
**State:** ✅ Complete
**Depends on:** none (depends on Task 05.0 being complete)
**Blocks:** 03

## Objective

Move `GET /r/{slug}` behavior into a dedicated handler without changing its
fast-path logic.

## Files to add

- `include/url_shortener/http/handlers/RedirectHandlers.hpp`
- `src/http/handlers/RedirectHandlers.cpp`
- `tests/unit/http/12_prefixed_redirect_handler.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/redirect_handlers.hpp` and
`src/http/handlers/redirect_handlers.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`); the test file matches the originally
proposed name and number.

## Files to modify

- `sources.cmake`
- `CMakeLists.txt`

## New function

```cpp
BeastResponse handlePrefixedRedirect(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    const RouteContext& context);
```

## Existing logic to move

From `handleShortenerRequest()` branch:

```cpp
if (target.rfind(std::string(short_redirect_prefix), 0) == 0) { ... }
```

Move only this behavior:

- slug validation with `isValidSlug`;
- link read with `getLinkForRead`;
- status evaluation with `resolveLinkStatus`;
- password/rate hooks;
- stats update;
- `updateLinkAndInvalidateCache`;
- `emitClickEvent`;
- response with `Location` and `keep_alive(false)`.

## Tests

Add tests for:

- active temporary redirect;
- active permanent redirect;
- invalid slug;
- missing link;
- deleted link;
- disabled link;
- expired link;
- blocked by policy hook behavior if injectable in current code.

## Constraints

- Handler file does not include link management handler headers.
- No JSON link serialization is added to redirect success path.

## Success criteria

- [x] `include/url_shortener/http/handlers/redirect_handlers.hpp` declares
      `handlePrefixedRedirect`.
- [x] `tests/unit/http/12_prefixed_redirect_handler.cpp` covers active
      temporary/permanent redirects and invalid/missing/deleted/disabled/
      expired link cases.
- [x] The handler file does not include link management handler headers.
- [x] No JSON link serialization is added to the redirect success path.
