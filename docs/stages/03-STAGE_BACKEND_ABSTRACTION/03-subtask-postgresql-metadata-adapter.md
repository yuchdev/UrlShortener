# Stage 03 Subtask 03 — PostgreSQL Metadata Adapter

## Purpose and Scope

This subtask introduces the durable production-grade metadata backend required by Stage 03. PostgreSQL becomes the canonical source of truth for link metadata in production-oriented deployments. The adapter must satisfy the same repository contract and behavioral guarantees as InMemory and SQLite while adding migration discipline and operational robustness appropriate for a networked SQL backend.

### In Scope
- Introduce exactly one PostgreSQL client dependency
- Implement `PostgresMetadataRepository`
- Add schema migrations for `links`
- Add reversible migration support or clearly structured up/down migration assets
- Implement error mapping from PostgreSQL client/library errors to `RepoError`
- Add prepared statements where practical
- Add bounded transient retry behavior for safe operations if required by current project conventions
- Add PostgreSQL-specific unit/integration tests
- Register PostgreSQL in shared metadata contract suite

### Out of Scope
- Redis
- YAML config parser beyond temporary/local constructor wiring if strictly needed
- distributed migrations orchestration
- production secrets management
- analytics repository

## Goals
1. Provide the production metadata backend required by Stage 03.
2. Preserve backend-agnostic service logic.
3. Validate migrations, network failure handling, and JSONB round-trip behavior.
4. Prove InMemory/SQLite/PostgreSQL converge on one repository contract.

## Non-Goals
1. Building a full ORM layer.
2. Implementing HA/replication/failover automation.
3. Solving long-term DBA concerns outside adapter needs.
4. Introducing distributed transactions.

## Dependency Budget

**Allowed new third-party dependency:** choose exactly one:
- `libpqxx`, or
- `libpq`

Do not add both.

## Mandatory Engineering Constraints

1. Introduce **at most one new third-party dependency** in this subtask.
2. Keep all pre-existing tests green.
3. Do not change product/business rules for short-link creation or redirect semantics unless explicitly required here.
4. Keep all backend-specific code inside infrastructure/adapter modules.
5. Do not reference backend-native headers, types, or constants from core/domain/application modules.
6. All public interfaces and non-trivial methods MUST have docstrings/comments consistent with project conventions.
7. Each test file listed below is mandatory unless the repository already contains an equivalent file with a stricter or more informative name.
8. Every unit/integration test MUST be deterministic and runnable in CI.
9. Mock tests MUST be used where network/process failures are simulated and a real backend is not required.
10. If a small design issue is discovered outside the task boundary, document it in the task output instead of silently broadening scope.

## Architecture and Design Requirements

### Adapter requirements
- Implement `IMetadataRepository`
- Keep PostgreSQL-native types hidden inside adapter
- Use prepared/parameterized statements
- Integrate with migration assets or migration utility already accepted by the repo
- Expose constructor/factory inputs needed for:
  - DSN / connection string
  - connect timeout
  - statement timeout
  - optional retry config
  - optional pool size / connection manager config if project already has such abstraction

### Required schema
At minimum:
- `short_code VARCHAR PRIMARY KEY`
- `target_url TEXT NOT NULL`
- `created_at TIMESTAMPTZ NOT NULL`
- `updated_at TIMESTAMPTZ NOT NULL`
- `expires_at TIMESTAMPTZ NULL`
- `is_active BOOLEAN NOT NULL`
- `owner_id VARCHAR NULL`
- `attributes_json JSONB NULL`

### Required operational behavior
- Connection failures fail clearly and observably
- Transient failures are mapped consistently
- Retry logic, if added, must be bounded and idempotent-safe
- Migrations must be versioned and reversible
- JSONB round-trip for `attributes` must be tested

### Error mapping
At minimum:
- unique violation -> `AlreadyExists`
- undefined table / broken schema -> startup/infrastructure failure mapped clearly
- serialization or transient connection interruption -> `TransientFailure` if retryable
- timeout/cancel -> `Timeout`
- permanent server-side error -> `PermanentFailure`

## Suggested Source Layout
- `src/storage/postgres/PostgresMetadataRepository.cpp`
- `include/.../storage/postgres/PostgresMetadataRepository.hpp`
- `db/migrations/postgres/001_create_links.up.sql`
- `db/migrations/postgres/001_create_links.down.sql`
- `test/unit/storage/psql/...`
- `test/integration/psql/...`

## Mandatory Unit Tests

### PostgreSQL adapter unit tests
- `test/unit/storage/psql/01_dsn_and_connection_options.cpp`
- `test/unit/storage/psql/02_create_and_get_roundtrip.cpp`
- `test/unit/storage/psql/03_duplicate_short_code_maps_to_already_exists.cpp`
- `test/unit/storage/psql/04_get_unknown_returns_empty.cpp`
- `test/unit/storage/psql/05_update_existing.cpp`
- `test/unit/storage/psql/06_delete_existing.cpp`
- `test/unit/storage/psql/07_exists_consistency.cpp`
- `test/unit/storage/psql/08_jsonb_attributes_roundtrip.cpp`
- `test/unit/storage/psql/09_optional_fields_roundtrip.cpp`
- `test/unit/storage/psql/10_error_mapping_transient_vs_permanent.cpp`
- `test/unit/storage/psql/11_prepared_statement_registration.cpp`
- `test/unit/storage/psql/12_retry_policy_bounds.cpp`

#### Required test cases
**Correct cases**
- valid connection options accepted
- create/get/update/delete behave according to repository contract
- JSONB attributes round-trip correctly
- optional fields round-trip correctly
- prepared statement registration succeeds without duplicate registration bugs
- retriable failure path does not exceed configured retry bound

**Incorrect / edge cases**
- duplicate short code returns `AlreadyExists`
- get unknown returns empty optional
- update/delete unknown behave according to contract
- malformed DSN/config fails clearly
- transient connection issue maps to transient error path
- non-retriable SQL error does not retry endlessly
- NULL JSONB / empty attributes map handled distinctly and consistently
- timestamp time zone handling does not drift unexpectedly

## Mandatory Contract Test Wiring

Register PostgreSQL backend with the shared suite:
- `test/contract/metadata/01_create_get_roundtrip.cpp`
- `test/contract/metadata/02_duplicate_short_code.cpp`
- `test/contract/metadata/03_get_unknown.cpp`
- `test/contract/metadata/04_update_existing.cpp`
- `test/contract/metadata/05_delete_removes_record.cpp`
- `test/contract/metadata/06_list_filtering_pagination.cpp`
- `test/contract/metadata/07_expiry_semantics.cpp`
- `test/contract/metadata/08_concurrent_create_one_wins.cpp`

## Mandatory Integration Tests

### PostgreSQL integration tests
- `test/integration/psql/01_connectivity_smoke.py`
- `test/integration/psql/02_migration_up_down_smoke.py`
- `test/integration/psql/03_connection_loss_and_retry.py`
- `test/integration/psql/04_jsonb_roundtrip.py`
- `test/integration/psql/05_contract_suite_runner.cpp`
- `test/integration/storage/08_postgres_service_full_flow.cpp`

#### Required integration scenarios
**Correct cases**
- test database is reachable and migratable
- migrations create expected schema and can roll back
- service can operate end-to-end with PostgreSQL metadata backend
- same repository contract suite passes against PostgreSQL
- JSONB attributes round-trip over real server

**Incorrect / edge cases**
- broken migration order fails loudly
- temporary connection loss triggers bounded retry behavior if implemented
- permanent auth/config failure is not misclassified as transient
- schema mismatch is surfaced clearly
- retry logic does not duplicate non-idempotent effects

## Acceptance Checklist
- [ ] PostgreSQL client library added as the only new dependency
- [ ] `PostgresMetadataRepository` implemented
- [ ] Versioned migrations added
- [ ] Error mapping and bounded retry behavior implemented and tested
- [ ] Shared contract suite passes for PostgreSQL
- [ ] End-to-end service integration works with PostgreSQL metadata backend

## Exit Criteria
This subtask is complete only when:
1. PostgreSQL passes the same repository behavior contract as in-memory and SQLite.
2. Migrations are versioned and reversible.
3. Core/service code remains backend-agnostic.
