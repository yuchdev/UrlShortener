# Stage 03 Subtask 02 — SQLite Metadata Adapter

## Purpose and Scope

This subtask introduces a realistic SQL metadata backend for local development, debugging, and CI-level relational behavior verification. SQLite is the first non-memory adapter and serves as the bridge between pure abstraction work and production-grade metadata persistence.

The adapter must satisfy the exact metadata repository contract established in Subtask 01 and must not require any changes to service logic or contract tests beyond backend factory registration.

### In Scope
- Introduce one SQLite dependency only
- Implement `SqliteMetadataRepository`
- Define and apply minimal SQLite schema/bootstrap logic for `links`
- Map SQLite-native errors into `RepoError`
- Support all required repository operations through parameterized statements
- Add SQLite-specific unit and integration tests
- Run shared metadata contract suite against SQLite
- Support local file-backed database initialization and busy timeout configuration

### Out of Scope
- PostgreSQL
- Redis
- YAML runtime config parser
- Cache adapter changes
- Analytics persistence changes

## Goals
1. Prove the repository abstraction works for a real SQL backend.
2. Enforce consistent behavior with in-memory backend through shared contract tests.
3. Add a practical developer backend without contaminating business logic.
4. Validate schema bootstrap, duplicate handling, and lock/busy semantics.

## Non-Goals
1. Production-grade connection pooling.
2. Cross-process migration orchestration.
3. SQL query optimization beyond obvious correctness.
4. Introducing ORM or heavy DB abstraction.

## Dependency Budget

**Allowed new third-party dependency:** SQLite / `sqlite3` only.

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
- The adapter must implement `IMetadataRepository` only.
- No SQLite types or constants may leak outside the adapter.
- Use parameterized queries exclusively.
- Busy timeout must be configurable within the adapter/factory-facing config structure used at this stage.
- Schema bootstrap may be automatic if enabled by simple config/constructor flag.

### Required schema
At minimum:
- `short_code TEXT PRIMARY KEY`
- `target_url TEXT NOT NULL`
- `created_at INTEGER NOT NULL`
- `updated_at INTEGER NOT NULL`
- `expires_at INTEGER NULL`
- `is_active INTEGER NOT NULL`
- `owner_id TEXT NULL`
- `attributes_json TEXT NULL`

### Error mapping
At minimum map:
- unique violation -> `AlreadyExists`
- lock/busy timeout -> `TransientFailure` or `Timeout` according to project taxonomy
- malformed input / validation mismatch -> `ValidationFailed` where appropriate
- generic SQLite failure -> `PermanentFailure` unless retriable

### Serialization requirements
- `attributes` may be serialized as JSON text
- nullability of optional fields must round-trip correctly
- timestamp precision must be documented and tested

## Suggested Source Layout
- `src/storage/sqlite/SqliteMetadataRepository.cpp`
- `include/.../storage/sqlite/SqliteMetadataRepository.hpp`
- `src/storage/sqlite/sql/001_init_links.sql` (or equivalent embedded migration asset)
- `test/unit/storage/sqlite/...`
- `test/integration/sqlite/...`

## Mandatory Unit Tests

### SQLite adapter unit tests
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

#### Required test cases
**Correct cases**
- new DB file initializes schema when auto-bootstrap enabled
- create/get round-trip preserves all fields
- update persists changed fields
- delete removes or hides record according to contract
- `Exists` matches `GetByShortCode`
- `attributes_json` round-trips flat map content
- `owner_id` and `expires_at` nullable fields round-trip correctly

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

Register SQLite backend with the shared suite from Subtask 01. The following files must execute against SQLite backend factory:
- `test/contract/metadata/01_create_get_roundtrip.cpp`
- `test/contract/metadata/02_duplicate_short_code.cpp`
- `test/contract/metadata/03_get_unknown.cpp`
- `test/contract/metadata/04_update_existing.cpp`
- `test/contract/metadata/05_delete_removes_record.cpp`
- `test/contract/metadata/06_list_filtering_pagination.cpp`
- `test/contract/metadata/07_expiry_semantics.cpp`
- `test/contract/metadata/08_concurrent_create_one_wins.cpp`

## Mandatory Integration Tests

### SQLite backend integration tests
- `test/integration/sqlite/01_db_file_initialization.py`
- `test/integration/sqlite/02_busy_lock_behavior.py`
- `test/integration/sqlite/03_contract_suite_runner.cpp`
- `test/integration/storage/07_sqlite_service_full_flow.cpp`

#### Required integration scenarios
**Correct cases**
- adapter creates/opens real file-backed DB and schema successfully
- service operates unchanged when metadata backend is SQLite
- same contract suite used for in-memory also passes against SQLite
- list/query operations behave consistently on real DB

**Incorrect / edge cases**
- busy DB / lock contention maps to expected error taxonomy
- startup with invalid path fails clearly
- schema already exists case is harmless
- partial/failed initialization does not leave undefined adapter state

## Acceptance Checklist
- [ ] SQLite added as the only new dependency
- [ ] `SqliteMetadataRepository` implemented
- [ ] Schema/bootstrap logic exists
- [ ] Error mapping implemented and tested
- [ ] Shared contract suite passes for SQLite
- [ ] Service-level integration flow works with SQLite metadata backend

## Exit Criteria
This subtask is complete only when:
1. SQLite passes all required unit tests and the shared contract suite.
2. Core/service code remains unchanged except for backend registration/wiring.
3. No backend-specific logic is added outside the SQLite adapter boundary.
