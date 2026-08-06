# Analytics Test Plan

## Scope

Analytics tests verify the best-effort redirect analytics pipeline, aggregate
stats reads, retention cleanup, sanitization, and privacy behavior.

## Required coverage

- Click event model and builder validation.
- Referrer, user-agent, and domain sanitization.
- Client identifier hashing without storing raw client identifiers.
- Queue enqueue/drop behavior and worker batching.
- Repository aggregate stats by status, domain, and time bucket.
- Disabled mode and repository failure isolation.
- CLI stats command validation and output where supported.

## Acceptance criteria

- Analytics failures do not change redirect responses.
- Queue overflow is observable in metrics/tests but does not block redirects.
- Aggregate stats output uses stable JSON fields.
- Raw IPs, unsalted identifiers, and secrets are not persisted or logged.

## E2E analogs

- `tests/e2e/scripts/sections/03_redirects.sh`
- `tests/e2e/scripts/sections/05_fingerprinting.sh`
- `tests/e2e/scripts/sections/09_error_handling.sh`
- `tests/e2e/scripts/sections/10_concurrency.sh`
