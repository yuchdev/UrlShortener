# Task 03.02 - Extract create, patch, and delete handlers

## Objective

Move canonical mutating link operations into named handlers.

## Files to modify

- `include/url_shortener/http/handlers/LinkHandlers.hpp`
- `src/http/handlers/LinkHandlers.cpp`
- `tests/unit/http/08_link_mutation_handlers.cpp`

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

## Acceptance criteria

- Compatibility `code` field is not handled here; that stays for Stage 04.
- No JSON dependency is introduced.

