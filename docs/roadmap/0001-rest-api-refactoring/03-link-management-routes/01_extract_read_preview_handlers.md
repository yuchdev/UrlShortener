# Task 03.01 - Extract canonical read and preview handlers

## Objective

Extract read-only canonical link handlers from the branch chain.

## Files to add

- `include/url_shortener/http/handlers/LinkHandlers.hpp`
- `src/http/handlers/LinkHandlers.cpp`
- `tests/unit/http/07_link_read_preview_handlers.cpp`

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

## Acceptance criteria

- Serialization output remains byte-compatible where tests assert it.
- No mutation occurs in these handlers.

