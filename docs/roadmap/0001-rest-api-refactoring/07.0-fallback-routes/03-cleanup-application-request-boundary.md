# 03 - Clarify handleApplicationRequest boundary

**Parent task:** 07.0 Fallback routes
**State:** ✅ Complete
**Depends on:** 01, 02
**Blocks:** none

## Objective

Keep or move fallback implementation deliberately after router migration.

## Files to review and possibly modify

- `include/url_shortener/http/request_handlers.h`
- `src/http/request_handlers.cpp`
- `include/url_shortener/http/handlers/FallbackHandlers.hpp`
- `src/http/handlers/FallbackHandlers.cpp`

Implementation note: the handler files shipped as
`include/url_shortener/http/handlers/fallback_handlers.hpp` and
`src/http/handlers/fallback_handlers.cpp` (renamed to snake_case by commit
`3f4c170 Rename files to snake_style`); `request_handlers.h`/`.cpp` match
their original names.

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

## Constraints

- The chosen boundary is documented in code comments.
- No duplicated URI-store implementation remains.

## Success criteria

- [x] Option A was selected: `handleApplicationRequest()` remains declared in
      `include/url_shortener/http/request_handlers.h` and implemented in
      `src/http/request_handlers.cpp`;
      `handleFallbackUriStore()` in `src/http/handlers/fallback_handlers.cpp`
      wraps it rather than duplicating `UriMapSingleton` logic.
- [x] Existing fallback tests
      (`tests/unit/http/14_fallback_handler.cpp`,
      `tests/unit/http/04_redirect_fallback_characterization.cpp`) pass.
- [x] No `request_handlers.cpp` branch calls fallback directly except
      through router dispatch - migration is complete
      (`handleShortenerRequest()` is a one-line delegation, see Task 08.0).
- [x] The chosen boundary is documented in code comments.
- [x] No duplicated URI-store implementation remains.
