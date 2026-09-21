# 01 - Extract canonical read and preview handlers

**Parent task:** 04.0 Link management routes
**State:** ✅ Complete
**Depends on:** none (depends on Task 03.0 being complete)
**Blocks:** 04

## Objective

Extract read-only canonical link handlers from the branch chain.

## Files to add

- `include/url_shortener/http/handlers/LinkHandlers.hpp`
- `src/http/handlers/LinkHandlers.cpp`
- `tests/unit/http/07_link_read_preview_handlers.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/link_handlers.hpp` and
`src/http/handlers/link_handlers.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`). The proposed test file was
consolidated with the Task 04.0 subtask 02 and 03 test files into a single
`tests/unit/http/10_link_handlers.cpp` - see the Task 04.0
[README.md](README.md) implementation note.

## Files to modify

- `sources.cmake`
- `CMakeLists.txt`

## New functions

```cpp
BeastResponse handleGetLinkById(...);
BeastResponse handleGetLinkBySlug(...);
BeastResponse handlePreviewLink(...);
```

Use `RouteContext::path_params`:

- `id` for `/api/v1/links/id/{id}`;
- `slug` for `/api/v1/links/{slug}`;
- `slug` for `/api/v1/links/{slug}/preview`.

## Existing logic to move

From `src/http/request_handlers.cpp::handleShortenerRequest()`:

- id lookup branch using `linkRepository().getById(id)`;
- slug lookup branch using `getLinkForRead(slug)`;
- preview JSON construction using `resolveLinkStatus`,
  `linkStatusToString`, and `redirectTypeToString`.

## Tests

Test:

- get by id success and not found;
- get by slug success and not found;
- preview active link;
- preview missing link;
- preview disabled/expired/deleted status serialization if test setup permits.

## Constraints

- Serialization output remains byte-compatible where tests assert it.
- No mutation occurs in these handlers.

## Success criteria

- [x] `include/url_shortener/http/handlers/link_handlers.hpp` declares
      `handleGetLinkById`, `handleGetLinkBySlug`, and `handlePreviewLink`.
- [x] `tests/unit/http/10_link_handlers.cpp` covers get-by-id, get-by-slug,
      and preview (active/missing/disabled/expired/deleted) cases.
- [x] Serialization output remains byte-compatible where tests assert it.
- [x] No mutation occurs in these handlers.
