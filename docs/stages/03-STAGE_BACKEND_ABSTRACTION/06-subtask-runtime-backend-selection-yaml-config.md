# Stage 03 Subtask 06 — Runtime Backend Selection and YAML Configuration

## Purpose and Scope

This subtask converts storage backend wiring from ad hoc/test-only construction into proper runtime configuration. After this task, metadata, cache, analytics, and optional rate limiter backends must be selectable through configuration without compile-time branching in the business layer.

### In Scope
- Introduce exactly one YAML/config parsing dependency
- Add typed configuration models for storage backends
- Parse YAML into typed config structures
- Validate backend names and backend-specific required fields
- Implement composition root / adapter factory wiring
- Add integration tests that prove backend swapping through config alone
- Add configuration documentation/examples used by tests

### Out of Scope
- New storage backends
- application feature changes
- adding a heavyweight DI framework
- distributed config management or secrets vault integration

## Goals
1. Fulfill the Stage 03 requirement for configuration-driven backend selection.
2. Prove the service can swap backends by configuration only.
3. Validate startup diagnostics and required field enforcement.
4. Centralize adapter creation outside business logic.

## Non-Goals
1. Designing a general-purpose config platform for the entire company.
2. Replacing all existing app configuration unrelated to storage.
3. Introducing hot-reload or dynamic live reconfiguration.
4. Adding more than one config dependency.

## Dependency Budget

**Allowed new third-party dependency:** `yaml-cpp` only.

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

### Required config coverage
At minimum support:
- metadata backend:
  - `inmemory`
  - `sqlite`
  - `postgres`
- cache backend:
  - `none`
  - `inmemory`
  - `redis`
- analytics backend:
  - `noop`
  - `inmemory`
  - optionally `stdout` if the repo already uses it
- rate limit:
  - `enabled`
  - backend selection if implemented

### Required validation rules
- invalid backend name -> startup failure with clear message
- missing required fields for selected backend -> startup failure
- unused sections may exist but must not silently override selected backend
- default values must be deterministic and documented

### Required wiring rules
- business/core modules receive already-constructed interfaces
- no compile-time backend branching in handlers/services/use-cases
- factories/composition root may depend on backend-specific headers

## Suggested Source Layout
- `include/.../config/StorageConfig.hpp`
- `src/config/StorageConfig.cpp`
- `src/composition/StorageFactory.cpp`
- `test/unit/config/...`
- `test/integration/config/...`
- `docs/storage/configuration.md`

## Mandatory Unit Tests
- `test/unit/config/01_parse_minimal_inmemory.cpp`
- `test/unit/config/02_parse_sqlite_config.cpp`
- `test/unit/config/03_parse_postgres_config.cpp`
- `test/unit/config/04_parse_redis_cache_config.cpp`
- `test/unit/config/05_invalid_backend_name.cpp`
- `test/unit/config/06_missing_required_fields.cpp`
- `test/unit/config/07_default_values.cpp`
- `test/unit/config/08_factory_selects_correct_backend_types.cpp`

#### Required test cases
**Correct cases**
- minimal in-memory config parses successfully
- sqlite/postgres/redis sections parse into typed structures
- defaults are applied as documented
- factory returns correct adapter implementations for valid config

**Incorrect / edge cases**
- unknown backend name rejected
- missing DSN/path/address rejected when selected backend needs it
- invalid numeric values (timeouts/pool sizes/TTL) rejected or normalized according to documented rules
- disabled rate limit does not require backend-specific fields
- extra irrelevant config does not accidentally override selected backend

## Mandatory Integration Tests
- `test/integration/config/01_inmemory_inmemory_from_yaml.cpp`
- `test/integration/config/02_sqlite_none_from_yaml.cpp`
- `test/integration/config/03_postgres_redis_from_yaml.cpp`
- `test/integration/config/04_invalid_config_startup_failure.cpp`
- `test/integration/storage/14_backend_swap_by_config_only.cpp`

#### Required integration scenarios
**Correct cases**
- service boots with in-memory metadata+cache from YAML only
- service boots with sqlite metadata and no cache from YAML only
- service boots with postgres metadata and redis cache from YAML only
- switching backend requires only config change, not service code change

**Incorrect / edge cases**
- invalid backend name causes startup failure
- missing required postgres DSN / sqlite path / redis address fails clearly
- conflicting or malformed config yields deterministic diagnostics
- wrong config does not partially construct app in broken state

## Acceptance Checklist
- [ ] YAML dependency added as the only new dependency
- [ ] Typed storage config models added
- [ ] Validation rules implemented
- [ ] Storage factory/composition root added
- [ ] Integration tests prove backend swap by config only
- [ ] Documentation examples match actual parser expectations

## Exit Criteria
This subtask is complete only when:
1. Storage backend selection is fully runtime-configurable.
2. Integration tests demonstrate backend swapping by configuration only.
3. Startup diagnostics are clear for incorrect configurations.
