# Task 00.03 - Characterize redirect and fallback behavior

## Objective

Create tests that prove redirect and fallback behavior before the fast path is
extracted or routed.

## Files to change

- `tests/unit/http/04_redirect_fallback_characterization.cpp` - new Boost.Test
  source.
- `CMakeLists.txt` - append the new file to `HTTP_UNIT_SOURCES`.

## Code under test

- `handleShortenerRequest(...)`
- `handleApplicationRequest(...)`
- `url_shortener::getLinkForRead(...)`
- `url_shortener::updateLinkAndInvalidateCache(...)`
- `url_shortener::redirectStatusFor(...)`
- `url_shortener::passwordGuardCheck(...)`
- `url_shortener::rateLimitGuardAllow(...)`

## Required redirect cases

- Active temporary link through `GET /r/{slug}` returns 302 with `Location`.
- Active permanent link through `GET /r/{slug}` returns 301 with `Location`.
- Active root redirect through `GET /{slug}` returns the same redirect status.
- Deleted link returns current deleted-link error.
- Disabled link returns current disabled-link error.
- Expired link returns current expired-link error.
- Missing prefixed slug returns current not-found error.
- Invalid prefixed slug returns current not-found error.

## Required fallback cases

- `POST /arbitrary-path` stores body through `handleApplicationRequest()`.
- `GET /arbitrary-path` returns stored body and content type.
- `DELETE /arbitrary-path` deletes stored data.
- `GET /` returns current root fallback behavior.
- `POST /` returns current root fallback behavior.

## Fast path assertions

- Redirect responses keep `keep_alive(false)`.
- Redirect responses set `Location`.
- Redirect responses do not include management-plane JSON link metadata.
- Counters increment as they do today.

## Acceptance criteria

- Tests document intentional root redirect fallthrough.
- Tests pass before redirect code is extracted.

