# 03 - Extract lifecycle, stats, and placeholder handlers

**Parent task:** 04.0 Link management routes
**State:** ✅ Complete
**Depends on:** 01, 02
**Blocks:** 04

## Objective

Move the remaining canonical `/api/v1/links/{slug}/...` action handlers.

## Files to modify

- `include/url_shortener/http/handlers/LinkHandlers.hpp`
- `src/http/handlers/LinkHandlers.cpp`
- `tests/unit/http/09_link_action_handlers.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/link_handlers.hpp` and
`src/http/handlers/link_handlers.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`). The proposed test file was
consolidated with the Task 04.0 subtask 01 and 02 test files into a single
`tests/unit/http/10_link_handlers.cpp` - see the Task 04.0
[README.md](README.md) implementation note.

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

## Constraints

- `handleLinkStats` receives query string from `RouteContext::query_string`.
- No route matching logic remains in these handler functions.

## Success criteria

- [x] `include/url_shortener/http/handlers/link_handlers.hpp` declares
      `handleEnableLink`, `handleDisableLink`, `handleRestoreLink`,
      `handleLinkStats`, and `handlePlaceholderFeature`.
- [x] `tests/unit/http/10_link_handlers.cpp` covers all seven listed cases.
- [x] `handleLinkStats` receives its query string from
      `RouteContext::query_string`.
- [x] No route matching logic remains in these handler functions.
