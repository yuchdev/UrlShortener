# Task 03.03 - Extract lifecycle, stats, and placeholder handlers

## Objective

Move the remaining canonical `/api/v1/links/{slug}/...` action handlers.

## Files to modify

- `include/url_shortener/http/handlers/LinkHandlers.hpp`
- `src/http/handlers/LinkHandlers.cpp`
- `tests/unit/http/09_link_action_handlers.cpp`

## New functions

```cpp
BeastResponse handleEnableLink(...);
BeastResponse handleDisableLink(...);
BeastResponse handleRestoreLink(...);
BeastResponse handleLinkStats(...);
BeastResponse handlePlaceholderFeature(...);
```

## Existing logic to move

From `handleShortenerRequest()`:

- `action == "enable"`;
- `action == "disable"`;
- `action == "restore"`;
- `action == "stats"`;
- `action == "qr" || action == "routing"`.

## Preserve exactly

- missing link returns `404 not_found`;
- lifecycle actions update `updated_at`;
- lifecycle actions call `updateLinkAndInvalidateCache`;
- stats with `from`, `to`, and `bucket` delegates to
  `http::AnalyticsStatsHandler`;
- stats without full query range returns legacy counters;
- placeholder routes validate link existence before returning
  `501 feature_not_enabled`.

## Tests

Add tests for:

- enable/disable/restore success;
- action missing slug;
- stats legacy counters;
- stats aggregate query validation errors;
- QR placeholder missing link;
- QR placeholder existing link returns 501;
- routing placeholder existing link returns 501.

## Acceptance criteria

- `handleLinkStats` receives query string from `RouteContext::query_string`.
- No route matching logic remains in these handler functions.

