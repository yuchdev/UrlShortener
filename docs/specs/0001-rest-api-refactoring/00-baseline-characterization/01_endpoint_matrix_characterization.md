# Task 00.01 - Characterize the REST endpoint matrix

## Objective

Add behavior-preserving tests that document every currently supported public
HTTP endpoint before dispatch code is moved.

## Files to change

- `tests/unit/http/02_endpoint_matrix_characterization.cpp` - new Boost.Test
  source.
- `CMakeLists.txt` - append the new file to `HTTP_UNIT_SOURCES`.

## Code under test

- Function: `url_shortener::handleShortenerRequest(...)`
- Function: `url_shortener::handleApplicationRequest(...)`
- Function: `url_shortener::routeLabelFor(...)`
- Data: `url_shortener::http::registeredRoutes()`

## Required test cases

Create one request helper in the test file:

```cpp
boost::beast::http::request<boost::beast::http::string_body>
make_request(boost::beast::http::verb method, std::string target,
             std::string body = "{}");
```

Cover these endpoints by method and target:

- `GET /healthz`
- `GET /readyz`
- `GET /metrics`
- `POST /api/v1/links`
- `GET /api/v1/links/id/{id}`
- `GET /api/v1/links/{slug}`
- `PATCH /api/v1/links/{slug}`
- `DELETE /api/v1/links/{slug}`
- `GET /api/v1/links/{slug}/preview`
- `GET /api/v1/links/{slug}/qr`
- `GET /api/v1/links/{slug}/routing`
- `GET /api/v1/links/{slug}/stats`
- `POST /api/v1/links/{slug}/enable`
- `POST /api/v1/links/{slug}/disable`
- `POST /api/v1/links/{slug}/restore`
- `POST /api/v1/short-urls`
- `GET /api/v1/short-urls/{slug}`
- `GET /r/{slug}`
- `GET /{slug}`
- `GET /{path}`
- `POST /{path}`
- `DELETE /{path}`

## Assertions

- Assert response status for each success path.
- Assert content type for JSON vs text responses.
- Assert `X-Request-Id` exists on responses that use `makeResponse()` or
  `makeApiErrorResponse()`.
- Assert representative JSON fields, not full JSON equivalence.
- Assert each target maps to the same label as `routeLabelFor(target)`.

## Acceptance criteria

- Tests pass before any router dispatch changes.
- No production files are changed by this task.
- The test file is registered under the `unit` CTest label.

