# Stage 03 Subtask 02 — SQL Metadata Adapter Foundation (SQLite-first via SOCI)

## Purpose and Scope

This subtask replaces the backend-specific SQLite-only design with a **single SQL repository architecture** that can support both SQLite and PostgreSQL through one C++ database access library. The selected dependency for this stage is **SOCI**. It must be fetched during the CMake prebuild/configure step and compiled with **SQLite enabled only in this subtask**. PostgreSQL support is intentionally deferred to the next subtask, but the class structure, factory boundaries, tests, and documentation introduced here MUST already be shaped for dual-backend use.

The outcome of this subtask is not “a SQLite adapter with SQL sprinkled around it.” The outcome is a **universal SQL metadata persistence layer** with:
- one repository contract,
- one SQL-facing implementation family,
- one backend-neutral configuration model for SQL repositories,
- one backend-neutral statement and mapping strategy,
- SQLite as the first concrete backend.

This subtask must not require later Stage 03 PostgreSQL work to redesign the service or replace repository interfaces again.

### In Scope
- Introduce exactly one new third-party dependency: **SOCI**
- Fetch SOCI from CMake during configure/prebuild
- Enable SOCI SQLite backend only in this subtask
- Replace SQLite-specific repository architecture with a universal SQL repository architecture
- Implement SQLite-backed concrete classes using the universal SQL architecture
- Define backend-neutral SQL schema/bootstrap abstraction for `links`
- Define backend-neutral row mapping and parameter binding boundaries
- Add SQLite-specific tests against the new SQL architecture
- Run shared metadata contract suite against the SQLite SQL backend
- Add documentation for later PostgreSQL extension without redesign

### Out of Scope
- SOCI PostgreSQL backend enablement
- live PostgreSQL connectivity
- Redis
- YAML runtime config parser
- analytics persistence changes
- connection pooling beyond interfaces/placeholders if not already present

## Goals
1. Establish one **SQL-first persistence architecture** shared by SQLite now and PostgreSQL later.
2. Prevent Stage 03 PostgreSQL work from introducing a second repository family or duplicated service wiring.
3. Keep business logic fully backend-agnostic.
4. Validate the architecture with a real SQLite implementation and full contract testing.
5. Make CMake dependency management deterministic by fetching SOCI during configure/prebuild.

## Non-Goals
1. Introducing a full ORM.
2. Hiding SQL semantics from developers.
3. Production-grade pooling in the SQLite phase.
4. Supporting multiple SQL libraries in parallel.
5. Solving all migration concerns for every backend in this subtask.

## Dependency Budget

**Allowed new third-party dependency:** **SOCI** only.

SOCI must be built with:
- SQLite backend: enabled
- PostgreSQL backend: disabled for this subtask
- unnecessary backends/features: disabled unless already mandated by repository conventions

Do **not** add raw `sqlite3` wrapper libraries, `libpqxx`, `libpq`, `sqlite_orm`, or any ORM/migration framework in this subtask.

## Mandatory Engineering Constraints

1. Introduce **at most one new third-party dependency** in this subtask.
2. The new dependency MUST be **SOCI**, fetched by CMake during configure/prebuild.
3. Keep all pre-existing tests green.
4. Do not change product/business rules for short-link creation or redirect semantics unless explicitly required here.
5. Keep all SQL-backend-specific code inside infrastructure/adapter modules.
6. Do not reference SOCI headers, session types, row types, backend IDs, or error categories from core/domain/application modules.
7. All public interfaces and non-trivial methods MUST have docstrings/comments consistent with project conventions.
8. Each test file listed below is mandatory unless the repository already contains an equivalent file with a stricter or more informative name.
9. Every unit/integration test MUST be deterministic and runnable in CI.
10. Mock tests MUST be used where backend behavior can be simulated without a live backend.
11. If a design issue is discovered outside the task boundary, document it in the task output instead of silently broadening scope.

## Architecture and Design Requirements

### Selected architectural direction

The prior backend-specific design (`SqliteMetadataRepository` as a concrete one-off adapter) is replaced by a **universal SQL repository family**.

Required design layers:

#### 1. Domain/Application boundary
- `IMetadataRepository`
- domain models / DTOs / repo error taxonomy
- no SOCI references
- no backend-specific config structures

#### 2. SQL infrastructure boundary
Introduce a backend-neutral SQL persistence layer such as:

- `SqlBackendKind`
- `SqlConnectionConfig`
- `SqlStatementCatalog`
- `SqlDialect`
- `SqlSessionFactory` or `ISqlSessionFactory`
- `SqlRowMapper`
- `SqlMetadataRepository`

These names may be adjusted to fit repo naming conventions, but the responsibilities are mandatory.

#### 3. SQLite backend plug-in
SQLite must be implemented as a backend specialization behind the SQL infrastructure boundary, for example:

- `SqliteSqlDialect`
- `SqliteSessionFactory`
- `SqliteErrorMapper`
- `SqliteBootstrapper`

The repository used by the service layer must still be the common SQL repository, not a service-visible SQLite-only class.

### Mandatory class responsibility split

#### `SqlConnectionConfig`
Backend-neutral structure carrying SQL connection settings needed now or later:
- backend kind (`sqlite` now, `postgres` later)
- DSN / connection string / file path abstraction
- connect timeout where meaningful
- statement timeout placeholder/value if meaningful
- busy timeout for SQLite
- auto-bootstrap / migrate-on-start flag
- retry policy placeholder/value for backends that support it later

This structure must be future-safe for PostgreSQL so Stage 03 Subtask 03 does not need to redefine configuration shape.

#### `SqlDialect`
Responsible for backend-specific SQL text and schema/bootstrap assets.
At minimum:
- create schema / bootstrap SQL
- parameter placeholder strategy if needed
- backend-specific column type choices
- backend-specific upsert/locking quirks hidden from repository logic where practical

#### `ISqlSessionFactory` / `SqlSessionFactory`
Responsible for creating configured SOCI sessions without leaking SOCI outside the SQL infrastructure module boundary.

#### `SqlMetadataRepository`
The main implementation of `IMetadataRepository` for SQL backends.
Responsibilities:
- create/get/update/delete/list/exists operations
- bind parameters through the chosen SQL layer
- map rows into domain records
- call dialect/bootstrap/error mapping services
- remain agnostic to whether backend is SQLite or PostgreSQL except through injected backend abstractions

#### Backend error mapper
SQLite-specific error mapping logic must be isolated so PostgreSQL can later plug into the same interface.

### Mandatory architectural rule

By the end of this subtask, the service layer must depend on:
- `IMetadataRepository`
- factory/wiring code that selects `SqlMetadataRepository` with SQLite backend configuration

The service layer must **not** depend on any class named `SqliteMetadataRepository`. A SQLite-specific leaf class may exist internally if absolutely needed, but it may not become the service-visible architectural center.

## CMake / Dependency Requirements

The build system must fetch SOCI during configure/prebuild via CMake, using a deterministic mechanism already accepted by the repo such as:
- `FetchContent`
- `ExternalProject`
- prebuild bootstrap helper invoked from CMake

The result must satisfy:
- reproducible CI setup
- pinned revision/version
- SQLite backend enabled
- PostgreSQL backend disabled for this subtask
- exported/intermediate targets documented

### Mandatory CMake test coverage
Add tests or validation logic that ensure:
- the build fails clearly if SOCI cannot be fetched
- SQLite backend is actually enabled in the fetched build
- accidental enabling of unrelated backends is avoided when reasonable

## Required schema

For SQLite in this subtask, the effective schema remains:

- `short_code TEXT PRIMARY KEY`
- `target_url TEXT NOT NULL`
- `created_at INTEGER NOT NULL`
- `updated_at INTEGER NOT NULL`
- `expires_at INTEGER NULL`
- `is_active INTEGER NOT NULL`
- `owner_id TEXT NULL`
- `attributes_json TEXT NULL`

However, the schema definition must now be represented through the universal SQL architecture so PostgreSQL can later provide its own dialect-specific type mapping without changing repository logic.

## Serialization requirements

- `attributes` may be serialized as JSON text in SQLite for now
- nullability of optional fields must round-trip correctly
- timestamp precision and epoch convention must be documented explicitly
- empty object vs null attributes behavior must be defined and tested
- malformed stored JSON must produce stable adapter failure, never crash

## Error mapping

At minimum map SQLite-originated failures into the repository error taxonomy:

- unique violation -> `AlreadyExists`
- lock / busy timeout -> `TransientFailure` or `Timeout` according to project taxonomy
- malformed input / validation mismatch -> `ValidationFailed` where appropriate
- misconfigured or inaccessible DB -> clear infrastructure failure
- generic backend failure -> `PermanentFailure` unless clearly retriable

The mapping mechanism must be designed so PostgreSQL can later reuse the same mapper interface.

## Suggested Source Layout

- `include/.../storage/sql/SqlConnectionConfig.hpp`
- `include/.../storage/sql/SqlBackendKind.hpp`
- `include/.../storage/sql/ISqlSessionFactory.hpp`
- `include/.../storage/sql/SqlDialect.hpp`
- `include/.../storage/sql/SqlMetadataRepository.hpp`
- `include/.../storage/sql/SqlMetadataRowMapper.hpp`
- `include/.../storage/sqlite/SqliteSqlDialect.hpp`
- `include/.../storage/sqlite/SqliteSessionFactory.hpp`
- `include/.../storage/sqlite/SqliteErrorMapper.hpp`
- `src/storage/sql/SqlMetadataRepository.cpp`
- `src/storage/sql/SqlMetadataRowMapper.cpp`
- `src/storage/sqlite/SqliteSqlDialect.cpp`
- `src/storage/sqlite/SqliteSessionFactory.cpp`
- `src/storage/sqlite/SqliteErrorMapper.cpp`
- `src/storage/sqlite/sql/001_init_links.sql`
- `cmake/FetchSOCI.cmake` (or repo-equivalent)
- `test/unit/storage/sql/...`
- `test/unit/storage/sqlite/...`
- `test/integration/sqlite/...`

If the repo naming style prefers `db/` or `infra/persistence/`, match that style, but keep the same architectural split.

## Mandatory Unit Tests

### SQL foundation unit tests
- `test/unit/storage/sql/01_connection_config_defaults.cpp`
- `test/unit/storage/sql/02_backend_kind_selection.cpp`
- `test/unit/storage/sql/03_sqlite_dialect_schema_sql.cpp`
- `test/unit/storage/sql/04_row_mapper_roundtrip.cpp`
- `test/unit/storage/sql/05_repository_uses_dialect_and_factory.cpp`
- `test/unit/storage/sql/06_error_mapper_interface_contract.cpp`

#### Required test cases
**Correct cases**
- backend-neutral SQL config accepts SQLite mode cleanly
- dialect returns schema/bootstrap SQL deterministically
- row mapper preserves all fields
- repository delegates backend concerns to injected dialect/factory services
- future-facing config fields do not break SQLite mode

**Incorrect / edge cases**
- unsupported backend selection in this subtask fails clearly
- missing/invalid SQLite connection input fails deterministically
- null vs empty attributes handling is explicit and stable
- timestamp conversion boundaries are documented and tested

### SQLite backend unit tests
- `test/unit/storage/sqlite/01_schema_bootstrap.cpp`
- `test/unit/storage/sqlite/02_create_and_get_roundtrip.cpp`
- `test/unit/storage/sqlite/03_duplicate_short_code_maps_to_already_exists.cpp`
- `test/unit/storage/sqlite/04_get_unknown_returns_empty.cpp`
- `test/unit/storage/sqlite/05_update_existing.cpp`
- `test/unit/storage/sqlite/06_delete_existing.cpp`
- `test/unit/storage/sqlite/07_exists_consistency.cpp`
- `test/unit/storage/sqlite/08_attributes_json_roundtrip.cpp`
- `test/unit/storage/sqlite/09_optional_fields_roundtrip.cpp`
- `test/unit/storage/sqlite/10_timestamp_roundtrip.cpp`
- `test/unit/storage/sqlite/11_busy_timeout_configuration.cpp`
- `test/unit/storage/sqlite/12_error_mapping_generic_failure.cpp`
- `test/unit/storage/sqlite/13_session_factory_builds_sqlite_session.cpp`
- `test/unit/storage/sqlite/14_sql_repository_with_sqlite_backend.cpp`

#### Required test cases
**Correct cases**
- new DB file initializes schema when auto-bootstrap enabled
- create/get round-trip preserves all fields
- update persists changed fields
- delete removes or hides record according to contract
- `Exists` matches `GetByShortCode`
- `attributes_json` round-trips flat map content
- `owner_id` and `expires_at` nullable fields round-trip correctly
- SQL repository works unchanged when injected with SQLite backend components

**Incorrect / edge cases**
- duplicate short code maps to `AlreadyExists`
- unknown short code returns empty optional
- update unknown returns correct contract error
- delete unknown returns correct contract error or idempotent behavior consistent with contract
- invalid DB path yields deterministic failure
- malformed stored JSON in `attributes_json` maps to stable adapter failure, not crash
- NULL vs empty-string behavior is explicitly tested and documented
- large URLs / long short codes behave according to domain validation rules, not SQLite quirks

## Mandatory Contract Test Wiring

Register the SQLite-backed universal SQL repository with the shared suite from Subtask 01.

The following files must execute against the SQLite backend factory:
- `test/contract/metadata/01_create_get_roundtrip.cpp`
- `test/contract/metadata/02_duplicate_short_code.cpp`
- `test/contract/metadata/03_get_unknown.cpp`
- `test/contract/metadata/04_update_existing.cpp`
- `test/contract/metadata/05_delete_removes_record.cpp`
- `test/contract/metadata/06_list_filtering_pagination.cpp`
- `test/contract/metadata/07_expiry_semantics.cpp`
- `test/contract/metadata/08_concurrent_create_one_wins.cpp`

The factory registration must already use the new SQL architecture naming, not backend-specific repository names.

## Mandatory Integration Tests

### Build / dependency integration
- `test/integration/build/01_fetch_soci_sqlite_only.py`
- `test/integration/build/02_sqlite_backend_enabled.py`

#### Required scenarios
**Correct cases**
- SOCI is fetched during configure/prebuild in CI
- SQLite backend is enabled in the fetched build
- build targets required by the app/tests are discoverable

**Incorrect / edge cases**
- network/fetch failure yields clear build/setup error
- missing SQLite backend in SOCI configuration fails loudly

### SQLite backend integration tests
- `test/integration/sqlite/01_db_file_initialization.py`
- `test/integration/sqlite/02_busy_lock_behavior.py`
- `test/integration/sqlite/03_contract_suite_runner.cpp`
- `test/integration/storage/07_sqlite_service_full_flow.cpp`

#### Required integration scenarios
**Correct cases**
- adapter creates/opens real file-backed DB and schema successfully
- service operates unchanged when metadata backend is SQLite through the universal SQL repository
- same contract suite used for in-memory also passes against SQLite SQL backend
- list/query operations behave consistently on real DB

**Incorrect / edge cases**
- busy DB / lock contention maps to expected error taxonomy
- startup with invalid path fails clearly
- schema already exists case is harmless
- partial/failed initialization does not leave undefined adapter state

## Mandatory Documentation Deliverables

- `docs/architecture/sql-persistence.md`
- `docs/architecture/sqlite-backend.md`
- `docs/build/fetch-soci.md`

### Documentation requirements
The docs must explicitly explain:
- why SOCI was selected
- why the repository architecture is now SQL-first rather than backend-specific
- how SQLite fits into a future PostgreSQL-capable shape
- where backend-specific code is allowed to live
- how to fetch/build SOCI via CMake
- which settings are already future-safe for PostgreSQL

## Acceptance Checklist
- [ ] SOCI added as the only new dependency
- [ ] SOCI fetched by CMake configure/prebuild logic
- [ ] Universal SQL persistence architecture implemented
- [ ] `SqlMetadataRepository` implemented
- [ ] SQLite backend components implemented under that architecture
- [ ] Schema/bootstrap logic exists through the dialect layer
- [ ] Error mapping implemented and tested
- [ ] Shared contract suite passes for SQLite
- [ ] Service-level integration flow works with SQLite metadata backend
- [ ] Documentation explains the path to PostgreSQL without redesign

## Exit Criteria

This subtask is complete only when:
1. SQLite passes all required unit tests and the shared contract suite through the new universal SQL architecture.
2. Core/service code remains unchanged except for backend registration/wiring to the SQL repository family.
3. No service-visible backend-specific logic is added outside the SQLite backend boundary.
4. Stage 03 Subtask 03 can add PostgreSQL support by plugging into the architecture created here, not by replacing it.
