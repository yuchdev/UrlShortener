# Task 00.02 - Characterize method and error behavior

## Objective

Lock down current wrong-method and validation-error responses so later router
work does not accidentally change client-visible behavior.

## Files to change

- `tests/unit/http/03_method_and_error_characterization.cpp` - new Boost.Test
  source.
- `CMakeLists.txt` - append the new file to `HTTP_UNIT_SOURCES`.

## Code under test

- `handleShortenerRequest(...)`
- `makeApiErrorResponse(...)`
- JSON helpers in `include/url_shortener/core/utils.h`

## Required cases

Wrong-method cases:

- `POST /healthz` returns current `400 invalid_method`.
- `POST /readyz` returns current `400 invalid_method`.
- `POST /metrics` returns current `400 invalid_method`.
- `POST /api/v1/links/id/{id}` returns current `400 invalid_method`.
- `POST /api/v1/links/{slug}/preview` returns current `400 invalid_method`.
- `GET /api/v1/links/{slug}/enable` returns current `400 invalid_method`.
- `POST /r/{slug}` returns current `400 invalid_method`.

Validation cases:

- create with missing `url` returns `400 invalid_url`.
- create with invalid `url` returns `400 invalid_url`.
- create with invalid `slug` returns `400 invalid_slug`.
- create with reserved `slug` returns `409 reserved_slug`.
- patch `enabled` with a non-boolean value returns `400 invalid_enabled`.
- patch `expires_at` with invalid timestamp returns `400 invalid_expires_at`.
- stats query with invalid timestamp returns current stats handler error.
- stats query with invalid bucket returns current stats handler error.

## Implementation notes

- Do not introduce a JSON parser.
- Check error codes with string search in `res.body()`.
- Reset or isolate global in-memory repository state between tests if needed by
  using unique slugs.

## Acceptance criteria

- Current behavior is asserted even if it is not ideal REST semantics.
- Do not change wrong-method responses to `405` in this task.

