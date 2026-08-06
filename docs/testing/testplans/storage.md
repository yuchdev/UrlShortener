# Storage and Database Test Plan

## Scope

Storage tests verify repository, cache, rate-limit, and SQL backend behavior
from isolated unit logic through full integration flows.

## Required coverage

- In-memory repository and cache semantics.
- SQL row mapping and backend-neutral repository behavior.
- SQLite schema bootstrap and state roundtrips.
- PostgreSQL dialect, migration runner, and repository roundtrips.
- Redis cache/rate-limiter serialization and fail-open behavior.
- Cache-aside read path and invalidation after update/delete.
- CLI-visible persistence for command-mode create/get scenarios.

## Acceptance criteria

- Backend behavior matches the metadata contract suite.
- Integration tests use temp state or documented local services.
- Database assertions are deterministic and avoid relying on row order unless
  explicitly specified.

## E2E analogs

- `tests/e2e/scripts/sections/02_create_short_url.sh`
- `tests/e2e/scripts/sections/04_expiration.sh`
- `tests/e2e/scripts/sections/10_concurrency.sh`
- `tests/e2e/scripts/sections/12_cli_link_get.sh`
