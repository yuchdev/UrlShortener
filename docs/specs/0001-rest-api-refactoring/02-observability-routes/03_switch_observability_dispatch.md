# Task 02.03 - Route observability requests through Router

## Objective

Make `handleShortenerRequest()` use the router for `/healthz`, `/readyz`, and
`/metrics`, while leaving all other routes on the old branch chain.

## Files to modify

- `src/http/request_handlers.cpp`
- `tests/unit/http/02_endpoint_matrix_characterization.cpp`
- `tests/unit/http/03_method_and_error_characterization.cpp`

## Required code change

In `handleShortenerRequest()`:

- create or retrieve a static application router built by
  `url_shortener::http::RouterBuilder::buildApplicationRouter()`;
- before the old literal observability branch, detect exact targets:
  - `/healthz`
  - `/readyz`
  - `/metrics`
- dispatch those requests through `router.dispatch(req, config, is_tls)`;
- remove or bypass the old observability branch only after tests pass.

Do not route any `/api`, redirect, or fallback request in this task.

## Tests

- Existing characterization tests must still pass.
- Add explicit assertions that wrong methods still return the current
  `400 invalid_method` shape.

## Acceptance criteria

- First production router usage is limited to three exact routes.
- No redirect or management behavior changes.

