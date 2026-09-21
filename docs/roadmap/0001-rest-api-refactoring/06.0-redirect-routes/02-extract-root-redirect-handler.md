# 02 - Extract root redirect handler with fallback

**Parent task:** 06.0 Redirect routes
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 03

## Objective

Move `GET /{slug}` behavior into a dedicated handler while preserving fallback
to `handleApplicationRequest()`.

## Files to modify

- `include/url_shortener/http/handlers/RedirectHandlers.hpp`
- `src/http/handlers/RedirectHandlers.cpp`
- `tests/unit/http/13_root_redirect_handler.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/redirect_handlers.hpp` and
`src/http/handlers/redirect_handlers.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`); the test file matches the originally
proposed name and number.

## New function

```cpp
BeastResponse handleRootRedirect(
    const BeastRequest& req,
    const ServerConfig& config,
    bool is_tls,
    const RouteContext& context);
```

## Existing logic to move

From `handleShortenerRequest()` branch:

```cpp
if (target.size() > 1 && target[0] == '/' && target.rfind("/api/", 0) != 0) {
    ...
}
```

Preserve:

- only `GET` attempts root redirect;
- non-GET falls back to `handleApplicationRequest`;
- invalid slug falls through to fallback behavior;
- missing valid slug returns current not-found behavior;
- active/disabled/expired/deleted handling matches today;
- stats update and analytics event emission match today.

## Tests

Add tests for:

- active root redirect;
- deleted/disabled/expired root slug;
- valid missing slug;
- invalid slug fallback;
- non-GET fallback;
- `/api/...` is not handled by root redirect.

## Constraints

- Root redirect fallthrough is explicit in the handler.
- No management-plane code is introduced.

## Success criteria

- [x] `include/url_shortener/http/handlers/redirect_handlers.hpp` declares
      `handleRootRedirect`.
- [x] `tests/unit/http/13_root_redirect_handler.cpp` covers all six listed
      cases, including non-GET and `/api/...` fallthrough.
- [x] Root redirect fallthrough is explicit in the handler.
- [x] No management-plane code is introduced.
