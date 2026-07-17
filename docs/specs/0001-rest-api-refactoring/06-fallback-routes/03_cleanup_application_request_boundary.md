# Task 06.03 - Clarify handleApplicationRequest boundary

## Objective

Keep or move fallback implementation deliberately after router migration.

## Files to review and possibly modify

- `include/url_shortener/http/request_handlers.h`
- `src/http/request_handlers.cpp`
- `include/url_shortener/http/handlers/FallbackHandlers.hpp`
- `src/http/handlers/FallbackHandlers.cpp`

## Options

Option A - keep `handleApplicationRequest()`:

- keep declaration in `request_handlers.h`;
- document it as a fallback implementation detail;
- `handleFallbackUriStore()` remains a wrapper.

Option B - move implementation:

- move `UriMapSingleton` read/write/delete logic into
  `FallbackHandlers.cpp`;
- remove `handleApplicationRequest()` declaration if no external callers remain;
- keep behavior byte-compatible.

## Required tests

Whichever option is selected:

- existing fallback tests must pass;
- no `request_handlers.cpp` branch should call fallback directly except through
  router dispatch if migration is complete.

## Acceptance criteria

- The chosen boundary is documented in code comments.
- No duplicated URI-store implementation remains.

