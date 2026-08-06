# REST API Test Plan

## Scope

REST API tests verify server-mode behavior and the HTTP adapter over the shared
application command layer.

## Required coverage

- Route registry enumerates all endpoints and metadata.
- Router matching and route context extraction.
- Link create/read/update/delete/lifecycle endpoints.
- Compatibility aliases under `/api/v1/short-urls`.
- Redirect endpoints under `/r/{slug}` and `/{slug}`.
- Stats endpoint validation and aggregate response mapping.
- Request hardening for oversized targets, bodies, and invalid request IDs.

## Acceptance criteria

- Existing HTTP status codes and JSON response shapes remain stable.
- Redirect fast-path tests do not depend on CLI or management-plane code.
- Compatibility aliases preserve existing client behavior.
- Error responses include stable codes and request IDs where expected.

## E2E analogs

- `tests/e2e/scripts/sections/01_server_boot.sh`
- `tests/e2e/scripts/sections/02_create_short_url.sh`
- `tests/e2e/scripts/sections/03_redirects.sh`
- `tests/e2e/scripts/sections/04_expiration.sh`
- `tests/e2e/scripts/sections/09_error_handling.sh`
- `tests/e2e/scripts/sections/10_concurrency.sh`
