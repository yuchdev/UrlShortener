# 02 - Extract create, patch, and delete handlers

**Parent task:** 04.0 Link management routes
**State:** ✅ Complete
**Depends on:** 01
**Blocks:** 04

## Objective

Move canonical mutating link operations into named handlers.

## Files to modify

- `include/url_shortener/http/handlers/LinkHandlers.hpp`
- `src/http/handlers/LinkHandlers.cpp`
- `tests/unit/http/08_link_mutation_handlers.cpp`

Implementation note: shipped as
`include/url_shortener/http/handlers/link_handlers.hpp` and
`src/http/handlers/link_handlers.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`). The proposed test file was
consolidated with the Task 04.0 subtask 01 and 03 test files into a single
`tests/unit/http/10_link_handlers.cpp` - see the Task 04.0
[README.md](README.md) implementation note.

## New functions

```cpp
BeastResponse handleCreateLink(...);
BeastResponse handlePatchLink(...);
BeastResponse handleDeleteLink(...);
```

## Existing logic to move

Move from `handleShortenerRequest()`:

- create logic for target `shortener_api_prefix`;
- patch logic inside `action.empty()` branch;
- delete logic inside `action.empty()` branch.

## Preserve exactly

- `extractJsonStringField(req.body(), "url")`;
- `normalizeTargetUrl(*raw_url, config)`;
- custom `slug` validation;
- reserved slug rejection;
- duplicate slug rejection;
- generated slug fallback via `generateUniqueSlug(config)`;
- redirect type parsing;
- default expiry handling;
- `enabled`, `tags`, `metadata`, and `campaign` parsing;
- `deleted_at` server-managed rejection;
- `updateLinkAndInvalidateCache(*link)`;
- response via `serializeLink(...)`.

## Tests

Add tests for:

- generated slug create;
- custom slug create;
- invalid URL;
- invalid slug;
- reserved slug;
- duplicate slug;
- patch each mutable field;
- invalid patch field shapes;
- delete missing slug;
- delete success sets `deleted_at`.

## Constraints

- Compatibility `code` field is not handled here; that stays for Task 05.0.
- No JSON dependency is introduced.

## Success criteria

- [x] `include/url_shortener/http/handlers/link_handlers.hpp` declares
      `handleCreateLink`, `handlePatchLink`, and `handleDeleteLink`.
- [x] `tests/unit/http/10_link_handlers.cpp` covers all ten listed mutation
      cases.
- [x] The compatibility `code` field is not handled by these handlers.
- [x] No JSON dependency is introduced.
