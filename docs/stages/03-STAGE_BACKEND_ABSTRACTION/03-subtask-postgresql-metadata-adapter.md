# Stage 03 Subtask 03 — PostgreSQL Enablement for Universal SQL Metadata Adapter (SOCI)

## Purpose and Scope

This subtask completes the universal SQL metadata persistence architecture introduced in the previous subtask by enabling **PostgreSQL** on the same C++ library dependency: **SOCI**.

This work must **not** introduce a second persistence architecture, a second service-visible metadata repository family, or PostgreSQL-specific service logic. Instead, PostgreSQL becomes a second concrete backend plugged into the already-established SQL repository design.

The outcome of this subtask is:
- SOCI extended to build with PostgreSQL backend support,
- PostgreSQL dialect/session/error-mapping/migration components added,
- the same `SqlMetadataRepository` used for both SQLite and PostgreSQL,
- shared contract tests proving that both SQL backends converge on one repository contract.

### In Scope
- Extend the SOCI-based build to include PostgreSQL backend support
- Implement PostgreSQL backend plug-ins for the universal SQL repository architecture
- Add versioned PostgreSQL migrations for `links`
- Add reversible migration support or clearly structured up/down migration assets
- Add PostgreSQL-specific error mapping under the same SQL error-mapper architecture
- Add prepared statement / statement catalog support where practical
- Add bounded retry behavior only where safe and consistent with the repo’s error taxonomy
- Register PostgreSQL in the shared metadata contract suite
- Update architecture and operations documentation for dual SQL backend support

### Out of Scope
- introducing `libpqxx` or replacing SOCI
- Redis
- distributed migrations orchestration
- secrets vault integration
- HA/failover automation
- analytics repository
- changing service/business rules

## Goals
1. Reuse the exact SQL repository architecture introduced in the SQLite-first subtask.
2. Make PostgreSQL the production-grade SQL backend without changing service semantics.
3. Prove SQLite and PostgreSQL now share one infrastructure abstraction and one repository contract.
4. Validate migrations, connectivity failures, retry boundaries, and JSON/JSONB semantics.
5. Keep the dependency tree simpler by retaining one cross-database library.

## Non-Goals
1. Building a full ORM.
2. Adding a second database access stack.
3. Introducing application-visible PostgreSQL-specific behavior.
4. Solving advanced operational DBA concerns outside adapter requirements.
5. Implementing distributed transaction semantics.

## Dependency Budget

**Allowed new third-party dependency:** none beyond **SOCI** already introduced in the previous subtask.

This subtask may extend the existing fetched SOCI build to enable:
- SQLite backend: still enabled
- PostgreSQL backend: enabled now

Do **not** add:
- `libpqxx`
- direct raw `libpq` abstractions in application code
- another wrapper library
- another ORM or migration framework

System packages required to build SOCI PostgreSQL backend may be added at CI/system dependency level if necessary, but the repository’s application/library dependency remains **SOCI**.

## Mandatory Engineering Constraints

1. Do not replace the SQL architecture introduced in the previous subtask.
2. Do not introduce a second service-visible repository implementation family.
3. Keep all pre-existing tests green.
4. Do not change product/business rules for short-link creation or redirect semantics unless explicitly required here.
5. Keep all backend-specific code inside infrastructure/adapter modules.
6. Do not reference SOCI backend details, PostgreSQL-native concepts, or migration details from core/domain/application modules.
7. All public interfaces and non-trivial methods MUST have docstrings/comments consistent with project conventions.
8. Each test file listed below is mandatory unless the repository already contains an equivalent file with a stricter or more informative name.
9. Every unit/integration test MUST be deterministic and runnable in CI.
10. Mock tests MUST be used where network failure simulation is possible without a live server.
11. If a design issue is discovered outside the task boundary, document it in the task output instead of silently broadening scope.

## Architecture and Design Requirements

### Mandatory architecture rule

The same `SqlMetadataRepository` introduced in the previous subtask must remain the repository implementation used by the service layer.

PostgreSQL support must be added by supplying PostgreSQL-specific implementations to the SQL infrastructure seam, for example:

- `PostgresSqlDialect`
- `PostgresSessionFactory`
- `PostgresErrorMapper`
- `PostgresMigrationRunner`

Equivalent names are acceptable if they fit repo conventions, but the responsibilities are mandatory.

### Required SQL infrastructure reuse

The following abstractions from the previous subtask must be reused rather than replaced:
- `SqlBackendKind`
- `SqlConnectionConfig`
- `ISqlSessionFactory` / `SqlSessionFactory`
- `SqlDialect`
- `SqlMetadataRepository`
- statement catalog / query registry abstraction
- row mapper abstraction
- backend error mapper interface

If gaps are discovered, extend these abstractions conservatively. Do not fork the design into “SQLite path” and “Postgres path.”

### PostgreSQL backend responsibilities

#### `PostgresSessionFactory`
Responsible for creating configured SOCI PostgreSQL sessions from the backend-neutral `SqlConnectionConfig`.

Supported config inputs must include:
- DSN / connection string
- connect timeout
- statement timeout
- optional application name if project style allows it
- optional retry policy config
- optional pool size / session manager config only if the existing architecture already has such a concept

#### `PostgresSqlDialect`
Responsible for:
- PostgreSQL-specific schema SQL
- placeholder / statement style as needed
- JSON/JSONB handling choices
- backend-specific timestamp representation
- backend-specific DDL for migrations/bootstrap

#### `PostgresMigrationRunner`
Responsible for applying versioned migration assets.
Requirements:
- versioned assets
- deterministic ordering
- rollback/down support
- startup failure on broken schema state
- no silent partial migration acceptance

#### `PostgresErrorMapper`
Responsible for translating PostgreSQL-originated failures, surfaced through SOCI/backend diagnostics, into the project’s repository error taxonomy.

### Prepared statement / statement catalog requirement

If the architecture introduced a statement catalog or query registry in the previous subtask, PostgreSQL must integrate with it. Statement text may be dialect-specific, but repository method flow must remain common where practical.

Prepared statements should be used where they improve clarity and safety without distorting the design.

## CMake / Dependency Requirements

The SOCI fetch/build logic introduced previously must be extended so CI and local builds can enable PostgreSQL backend support deterministically.

### Mandatory build expectations
- pinned SOCI version/revision remains stable
- PostgreSQL backend is enabled
- SQLite backend remains enabled
- unrelated backends remain disabled unless explicitly justified
- system package prerequisites for PostgreSQL client development headers/libraries are documented and installed in CI as needed

### Mandatory build/integration verification
Add tests or validation checks that confirm:
- SOCI PostgreSQL backend is actually built/linked
- accidental removal of SQLite support is detected
- misconfigured PostgreSQL client prerequisites fail clearly

## Required schema

At minimum the PostgreSQL schema for `links` must provide:

- `short_code VARCHAR PRIMARY KEY`
- `target_url TEXT NOT NULL`
- `created_at TIMESTAMPTZ NOT NULL`
- `updated_at TIMESTAMPTZ NOT NULL`
- `expires_at TIMESTAMPTZ NULL`
- `is_active BOOLEAN NOT NULL`
- `owner_id VARCHAR NULL`
- `attributes_json JSONB NULL`

This schema must be delivered via migration assets, not ad hoc scattered SQL strings.

## Migration Requirements

### Mandatory migration files
- `db/migrations/postgres/001_create_links.up.sql`
- `db/migrations/postgres/001_create_links.down.sql`

Additional migrations may be added if required by the repo structure, but the minimum versioned up/down pair is mandatory.

### Migration behavior requirements
- migrations are versioned and deterministic
- up/down order is explicit
- repeated startup on already-migrated DB is harmless
- broken ordering or missing dependency fails loudly
- schema mismatch is surfaced clearly
- partial migration state does not masquerade as healthy startup

## Serialization requirements

- PostgreSQL must use JSONB for `attributes_json`
- nullability of optional fields must round-trip correctly
- timestamp time zone semantics must be documented and tested
- empty object vs null attributes behavior must match contract/documentation
- the repository contract visible to the service layer must remain consistent with SQLite even if backend storage differs

## Error mapping

At minimum map PostgreSQL-originated failures into the repository error taxonomy:

- unique violation -> `AlreadyExists`
- undefined table / broken schema -> startup or infrastructure failure mapped clearly
- serialization failure / deadlock / transient connection interruption -> `TransientFailure` if retryable
- timeout / cancel / statement timeout -> `Timeout`
- authentication / configuration failure -> clear permanent infrastructure failure
- generic unrecoverable server-side error -> `PermanentFailure`

The implementation must distinguish transient vs permanent failures conservatively and explicitly.

## Retry Behavior

Retry logic is optional unless required by current project conventions, but if introduced it must satisfy all of the following:

- implemented inside SQL/PostgreSQL infrastructure, not service logic
- bounded by config
- safe for idempotent operations only, or otherwise explicitly justified
- fully tested
- never retries endlessly
- never hides permanent auth/config/schema failures as transient

## Suggested Source Layout

- `include/.../storage/postgres/PostgresSqlDialect.hpp`
- `include/.../storage/postgres/PostgresSessionFactory.hpp`
- `include/.../storage/postgres/PostgresErrorMapper.hpp`
- `include/.../storage/postgres/PostgresMigrationRunner.hpp`
- `src/storage/postgres/PostgresSqlDialect.cpp`
- `src/storage/postgres/PostgresSessionFactory.cpp`
- `src/storage/postgres/PostgresErrorMapper.cpp`
- `src/storage/postgres/PostgresMigrationRunner.cpp`
- `db/migrations/postgres/001_create_links.up.sql`
- `db/migrations/postgres/001_create_links.down.sql`
- `test/unit/storage/psql/...`
- `test/integration/psql/...`

If the repo’s preferred naming is `postgresql/` instead of `psql/`, or `db/sql/postgres/`, keep naming consistent with the repo while preserving the same responsibilities.

## Mandatory Unit Tests

### SQL dual-backend architecture tests
- `test/unit/storage/sql/07_backend_registry_supports_sqlite_and_postgres.cpp`
- `test/unit/storage/sql/08_postgres_config_roundtrip.cpp`
- `test/unit/storage/sql/09_sql_repository_backend_switching.cpp`

#### Required test cases
**Correct cases**
- backend-neutral config supports both SQLite and PostgreSQL
- repository wiring selects correct backend services without service-layer changes
- PostgreSQL-specific options do not break SQLite mode

**Incorrect / edge cases**
- invalid backend selection fails clearly
- missing PostgreSQL-required config fields fail deterministically
- mixed/incoherent config is rejected

### PostgreSQL backend unit tests
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
- `test/unit/storage/psql/13_migration_runner_ordering.cpp`
- `test/unit/storage/psql/14_sql_repository_with_postgres_backend.cpp`

#### Required test cases
**Correct cases**
- valid connection options accepted
- create/get/update/delete behave according to repository contract
- JSONB attributes round-trip correctly
- optional fields round-trip correctly
- prepared statement registration succeeds without duplicate registration bugs
- migration runner orders assets deterministically
- retriable failure path does not exceed configured retry bound
- SQL repository works unchanged when injected with PostgreSQL backend components

**Incorrect / edge cases**
- duplicate short code returns `AlreadyExists`
- get unknown returns empty optional
- update/delete unknown behave according to contract
- malformed DSN/config fails clearly
- transient connection issue maps to transient error path
- non-retriable SQL error does not retry endlessly
- NULL JSONB / empty attributes map handled distinctly and consistently
- timestamp time zone handling does not drift unexpectedly
- broken migration ordering fails loudly
- schema mismatch is surfaced as infrastructure failure, not domain failure

## Mandatory Contract Test Wiring

Register PostgreSQL backend with the shared suite using the same universal SQL repository family:
- `test/contract/metadata/01_create_get_roundtrip.cpp`
- `test/contract/metadata/02_duplicate_short_code.cpp`
- `test/contract/metadata/03_get_unknown.cpp`
- `test/contract/metadata/04_update_existing.cpp`
- `test/contract/metadata/05_delete_removes_record.cpp`
- `test/contract/metadata/06_list_filtering_pagination.cpp`
- `test/contract/metadata/07_expiry_semantics.cpp`
- `test/contract/metadata/08_concurrent_create_one_wins.cpp`

The contract factory registration must prove that:
- in-memory backend still works,
- SQLite SQL backend works,
- PostgreSQL SQL backend works,
- all three satisfy one repository contract.

## Mandatory Integration Tests

### Build / dependency integration
- `test/integration/build/03_fetch_soci_sqlite_and_postgres.py`
- `test/integration/build/04_postgres_backend_enabled.py`

#### Required scenarios
**Correct cases**
- SOCI build includes both SQLite and PostgreSQL support
- CI installs/builds prerequisites needed for PostgreSQL backend support
- required link targets are available to tests and application binaries

**Incorrect / edge cases**
- missing PostgreSQL client prerequisites fail with clear diagnostics
- build misconfiguration that disables PostgreSQL backend is caught

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
- service can operate end-to-end with PostgreSQL metadata backend through the universal SQL repository
- same repository contract suite passes against PostgreSQL
- JSONB attributes round-trip over real server

**Incorrect / edge cases**
- broken migration order fails loudly
- temporary connection loss triggers bounded retry behavior if implemented
- permanent auth/config failure is not misclassified as transient
- schema mismatch is surfaced clearly
- retry logic does not duplicate non-idempotent effects

## Mandatory Documentation Deliverables

- `docs/architecture/sql-persistence.md` (update)
- `docs/architecture/postgres-backend.md`
- `docs/operations/postgres-local-dev.md`
- `docs/build/fetch-soci.md` (update)

### Documentation requirements
The docs must explicitly explain:
- why one cross-database library is retained
- how SQLite and PostgreSQL share one repository architecture
- which classes are common SQL infrastructure and which are backend plug-ins
- how migrations are organized and executed
- what PostgreSQL-specific config fields mean
- what retry behavior exists and where it is allowed
- what system packages/CI setup are required for PostgreSQL backend support

## Acceptance Checklist
- [ ] No new application/library dependency added beyond SOCI
- [ ] SOCI build extended to enable PostgreSQL backend
- [ ] PostgreSQL backend plug-ins implemented for the universal SQL repository architecture
- [ ] Versioned PostgreSQL migrations added
- [ ] Error mapping implemented and tested
- [ ] Retry behavior, if present, is bounded and tested
- [ ] Shared contract suite passes for PostgreSQL
- [ ] End-to-end service integration works with PostgreSQL metadata backend
- [ ] Documentation updated for dual SQL backend operation

## Exit Criteria

This subtask is complete only when:
1. PostgreSQL passes the same repository behavior contract as in-memory and SQLite using the same `SqlMetadataRepository` family.
2. Migrations are versioned and reversible.
3. Core/service code remains backend-agnostic.
4. The project now supports SQLite and PostgreSQL through one fetched CMake dependency and one coherent SQL persistence architecture.
