# Task 05.02 - Extract root redirect handler with fallback

## Objective

Move `GET /{slug}` behavior into a dedicated handler while preserving fallback
to `handleApplicationRequest()`.

## Files to modify

- `include/url_shortener/http/handlers/RedirectHandlers.hpp`
- `src/http/handlers/RedirectHandlers.cpp`
- `tests/unit/http/13_root_redirect_handler.cpp`

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

## Acceptance criteria

- Root redirect fallthrough is explicit in the handler.
- No management-plane code is introduced.

