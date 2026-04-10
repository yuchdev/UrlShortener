# Stage 03 Subtask 07 — Hardening, Documentation, Observability, and CI Matrix

## Purpose and Scope

This subtask completes Stage 03 as a coherent, operationally usable milestone. It does not introduce new backend functionality. Instead, it consolidates tests, documentation, observability, and CI execution so Stage 03 is reviewable, reproducible, and safe to extend.

### In Scope
- Complete storage documentation set
- Harden test matrix and CI jobs
- Add or finalize observability hooks required by Stage 03
- Ensure all backend-specific and shared tests are connected to CI
- Document how to add a new adapter
- Validate that acceptance and exit criteria for Stage 03 are demonstrably satisfied

### Out of Scope
- New backends
- new business features
- major storage model changes
- Stage 04 analytics repository work

## Goals
1. Convert the implementation into a maintainable documented system.
2. Ensure CI provides evidence for backend contract compliance.
3. Expose minimal metrics/logging hooks for operations and debugging.
4. Make future adapter addition straightforward.

## Non-Goals
1. Full production SRE stack.
2. Full admin dashboards.
3. Rewriting test framework choices.
4. Introducing new dependencies unless already present in the repo for CI scripts.

## Dependency Budget

**Allowed new third-party dependencies:** none.

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

## Documentation Deliverables

The following files are mandatory:
- `docs/storage/overview.md`
- `docs/storage/configuration.md`
- `docs/storage/adapters/sqlite.md`
- `docs/storage/adapters/postgres.md`
- `docs/storage/adapters/redis.md`
- `docs/storage/testing.md`
- `docs/storage/how-to-add-adapter.md`

### Documentation content requirements

#### `docs/storage/overview.md`
Must explain:
- ports-and-adapters architecture
- boundary between core/application/infrastructure
- why PostgreSQL is authoritative and Redis is not
- how cache-aside works
- failure semantics summary

#### `docs/storage/configuration.md`
Must explain:
- YAML examples for all supported backend combinations
- default values
- required vs optional settings
- startup failure examples for invalid configs

#### Adapter docs
Each adapter doc must explain:
- purpose
- supported capabilities
- operational limitations
- configuration fields
- test coverage summary
- failure semantics
- when to use / when not to use

#### `docs/storage/testing.md`
Must explain:
- unit vs contract vs integration test strategy
- how each backend reuses the same contract suite
- local commands to run storage-related tests
- expected external services for PostgreSQL/Redis tests

#### `docs/storage/how-to-add-adapter.md`
Must provide a checklist:
- implement interface
- map errors
- register in config factory
- wire contract tests
- add backend-specific unit tests
- add integration tests
- update docs and CI

## Observability Requirements

At minimum expose and/or stub:
- repository latency by backend and operation
- repository error count by backend and operation
- cache hit/miss count
- cache failure count
- analytics enqueue/drop count if analytics sink is async/buffered
- rate limit allow/deny count if rate limiting exists

Logging requirements:
- structured backend identifier in storage errors
- do not log secrets or raw sensitive payloads
- startup logs indicate selected backends

Health/readiness guidance:
- readiness should check selected backend dependencies within bounded timeout
- liveness should avoid deep backend checks

## Mandatory Unit Tests
- `test/unit/observability/01_repository_metrics_labels.cpp`
- `test/unit/observability/02_cache_metrics_labels.cpp`
- `test/unit/observability/03_startup_logging_redaction.cpp`
- `test/unit/docs/01_configuration_examples_match_parser.cpp`

#### Required test cases
**Correct cases**
- expected metric labels/tags are generated
- config examples used in docs parse correctly if parser tests can reuse them
- startup log output identifies backends without secrets

**Incorrect / edge cases**
- sensitive DSN/password content is redacted
- unknown backend metric tag values do not break instrumentation
- doc examples do not drift from actual accepted config

## Mandatory Integration Tests
- `test/integration/ci/01_inmemory_job_runner.py`
- `test/integration/ci/02_sqlite_job_runner.py`
- `test/integration/ci/03_postgres_job_runner.py`
- `test/integration/ci/04_redis_job_runner.py`
- `test/integration/ci/05_full_storage_matrix.py`

#### Required integration scenarios
**Correct cases**
- CI executes in-memory-only fast path
- CI executes SQLite path
- CI executes PostgreSQL contract/integration path when service available
- CI executes Redis cache/rate-limit path when service available
- full storage matrix summarizes pass/fail clearly

**Incorrect / edge cases**
- unavailable external services produce deterministic CI skip/fail behavior according to repo policy
- job orchestration does not silently pass without running backend-specific tests
- docs/test mismatch is caught by CI

## Acceptance Checklist
- [ ] Documentation set completed
- [ ] Observability hooks added or finalized
- [ ] CI matrix includes required backend jobs
- [ ] Docs/examples validated against parser/tests
- [ ] Stage 03 exit criteria are traceable to CI/test evidence

## Exit Criteria
This subtask is complete only when:
1. Stage 03 can be understood and operated from docs without reverse-engineering the code.
2. CI demonstrates backend coverage and contract compliance.
3. Observability/logging basics exist for storage backends and cache.
