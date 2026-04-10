# Stage 03 Subtask 01 — Core Ports, InMemory Adapters, and Shared Contract Test Harness

## Purpose and Scope

This subtask introduces the storage abstraction foundation required by Stage 03. It must separate core service logic from storage implementation details and provide a fully functional in-memory backend for fast development, deterministic tests, and CI smoke coverage.

This subtask is the architectural pivot of Stage 03. After it lands, the service must be able to operate exclusively through abstractions for metadata, cache, analytics sink, and optional rate limiting. No SQL, Redis, YAML, or production backend code belongs here.

### In Scope
- Introduce storage and side-effect ports:
  - `IMetadataRepository`
  - `ICacheStore`
  - `IAnalyticsSink`
  - `IRateLimiter` (optional but defined now)
- Introduce shared domain/application models required by the ports:
  - `LinkRecord`
  - `CreateLinkRequest`
  - `ListLinksQuery`
  - `CacheValue`
  - `LinkAccessEvent`
  - `RateLimitDecision`
- Introduce shared error taxonomy:
  - `RepoError`
  - `CacheError`
  - `AnalyticsError`
  - `RateLimitError`
- Refactor service/use-case layer to depend only on the above interfaces
- Implement:
  - `InMemoryMetadataRepository`
  - `InMemoryCacheStore`
  - `InMemoryAnalyticsSink`
  - `InMemoryRateLimiter` (optional, but preferred)
- Add deterministic time/clock injection where needed
- Create reusable metadata repository contract tests
- Create service-level integration tests for cache-aside behavior and redirect-visible semantics

### Out of Scope
- SQLite adapter
- PostgreSQL adapter
- Redis cache adapter
- Real YAML configuration parsing
- Production connection pools
- Analytics persistence beyond best-effort in-memory sink

## Goals
1. Core logic becomes backend-agnostic.
2. In-memory backend supports all semantics needed by later backend contract tests.
3. Shared contract test harness exists before SQL adapters are added.
4. Cache is introduced as an optimization boundary, not as a source of truth.
5. The codebase becomes ready for backend swapping without another large refactor.

## Non-Goals
1. Optimizing performance for large datasets.
2. Adding storage-specific tuning knobs.
3. Implementing production durability.
4. Introducing asynchronous pipelines or distributed coordination.

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

## Architecture and Design Requirements

### Required Layering
- **Core/domain/application code** may depend only on interface headers and portable shared models.
- **Infrastructure/in-memory code** may implement these interfaces using STL containers, locks, and clock injection.
- **Tests** must validate both interface-level semantics and service-level behavior.

### Required Implementation Behavior

#### Metadata semantics
- `CreateLink` must reject duplicate short codes.
- `GetByShortCode` must return empty optional for unknown records, not `NotFound`, unless the existing result abstraction requires a different project-wide convention.
- `UpdateLink` must persist all mutable fields allowed by the domain.
- `DeleteLink` must remove the record or mark it deleted according to existing service semantics, but external behavior must match the current product rules.
- `ListLinks` must support at least the minimal filters/query shape already defined by the project.
- `Exists` must be consistent with `GetByShortCode`.

#### Cache semantics
- Cache lookup must be optional and fail-open by default.
- Cache values for redirect path must include:
  - `target_url`
  - `expires_at`
  - `is_active`
  - optional version/etag when practical
- Cache invalidation must be deterministic after update/delete/disable flows.

#### Analytics semantics
- Analytics sink is best-effort.
- Analytics failure must not break redirect correctness.
- Sink may be a no-op or record events into an in-memory vector/queue for test verification.

#### Time semantics
- Expiry tests must use injected deterministic clock, not wall time sleeps.
- TTL handling in the in-memory cache may use lazy eviction on read.

#### Concurrency semantics
- In-memory repository and cache must be thread-safe.
- Minimal locking is acceptable; correctness is mandatory.

## Suggested Source Layout

- `include/.../storage/IMetadataRepository.hpp`
- `include/.../storage/ICacheStore.hpp`
- `include/.../storage/IAnalyticsSink.hpp`
- `include/.../storage/IRateLimiter.hpp`
- `include/.../storage/models/LinkRecord.hpp`
- `include/.../storage/models/CreateLinkRequest.hpp`
- `include/.../storage/models/ListLinksQuery.hpp`
- `include/.../storage/models/CacheValue.hpp`
- `include/.../storage/models/LinkAccessEvent.hpp`
- `include/.../storage/errors/RepoError.hpp`
- `include/.../storage/errors/CacheError.hpp`
- `src/storage/inmemory/InMemoryMetadataRepository.cpp`
- `src/storage/inmemory/InMemoryCacheStore.cpp`
- `src/storage/inmemory/InMemoryAnalyticsSink.cpp`
- `src/storage/inmemory/InMemoryRateLimiter.cpp`
- `test/unit/core/...`
- `test/unit/storage/inmemory/...`
- `test/contract/metadata/...`
- `test/integration/storage/...`

## Mandatory Unit Tests

### 1. Core model and interface tests
- `test/unit/core/01_result_error_taxonomy.cpp`
- `test/unit/core/02_link_record_validation.cpp`
- `test/unit/core/03_cache_value_model.cpp`
- `test/unit/core/04_link_access_event_model.cpp`
- `test/unit/core/05_interface_smoke_compile.cpp`

#### Required test cases
**Correct cases**
- `LinkRecord` stores required fields correctly
- optional fields can be absent
- error enums / categories are constructible and comparable
- interfaces are mockable/subclassable
- `CacheValue` can represent active and expiring links

**Incorrect / edge cases**
- invalid short code rejected if validation belongs in shared model
- empty target URL rejected if validation belongs in shared model
- expiry timestamp before creation handled according to current validation rules
- malformed attributes map does not crash serializers/equality helpers

### 2. In-memory metadata repository tests
- `test/unit/storage/inmemory/01_create_and_get_roundtrip.cpp`
- `test/unit/storage/inmemory/02_duplicate_short_code.cpp`
- `test/unit/storage/inmemory/03_get_unknown.cpp`
- `test/unit/storage/inmemory/04_update_existing.cpp`
- `test/unit/storage/inmemory/05_delete_existing.cpp`
- `test/unit/storage/inmemory/06_exists_consistency.cpp`
- `test/unit/storage/inmemory/07_list_filtering_and_pagination.cpp`
- `test/unit/storage/inmemory/08_expiry_semantics.cpp`
- `test/unit/storage/inmemory/09_thread_safety_smoke.cpp`

#### Required test cases
**Correct cases**
- create record then retrieve same record
- update mutable fields and retrieve updated state
- delete existing record and confirm absence
- `Exists` is true after create and false after delete
- list supports expected ordering/filtering/pagination contract
- expired record semantics match current service expectations

**Incorrect / edge cases**
- duplicate create returns `AlreadyExists`
- get unknown returns empty optional
- update unknown returns expected error outcome
- delete unknown returns expected idempotent or error behavior consistent with project
- create with optional empty owner/attributes succeeds
- concurrent create for same short code leads to exactly one winner
- concurrent reads while updating do not crash or corrupt memory

### 3. In-memory cache tests
- `test/unit/storage/inmemory/10_cache_get_set_delete.cpp`
- `test/unit/storage/inmemory/11_cache_ttl_lazy_eviction.cpp`
- `test/unit/storage/inmemory/12_cache_clear_by_prefix.cpp`
- `test/unit/storage/inmemory/13_cache_fail_open_behavior.cpp`

#### Required test cases
**Correct cases**
- set/get returns stored value before TTL expiry
- delete removes key
- clear by prefix removes only matching keys
- TTL expiration lazily evicts expired value

**Incorrect / edge cases**
- get missing key returns empty optional
- deleting missing key does not crash
- zero/negative-equivalent TTL behavior follows documented implementation rule
- prefix clear with no matches is harmless

### 4. In-memory analytics tests
- `test/unit/storage/inmemory/14_analytics_emit_and_flush.cpp`

#### Required test cases
**Correct cases**
- emit stores event
- flush succeeds as no-op or queue flush
- multiple events preserve insertion order if the implementation promises it

**Incorrect / edge cases**
- flush on empty sink succeeds
- large number of events remains bounded if bounded queue chosen

### 5. In-memory rate limiter tests
- `test/unit/storage/inmemory/15_rate_limit_allow_window.cpp`

#### Required test cases
**Correct cases**
- requests under limit are allowed
- request at boundary behaves according to defined policy
- counter resets after window using injected clock

**Incorrect / edge cases**
- limit zero behavior is explicitly tested
- malformed/empty key behavior documented and tested
- very large limit/window values do not overflow internal counters

## Mandatory Contract Tests

Create reusable contract suite under:
- `test/contract/metadata/01_create_get_roundtrip.cpp`
- `test/contract/metadata/02_duplicate_short_code.cpp`
- `test/contract/metadata/03_get_unknown.cpp`
- `test/contract/metadata/04_update_existing.cpp`
- `test/contract/metadata/05_delete_removes_record.cpp`
- `test/contract/metadata/06_list_filtering_pagination.cpp`
- `test/contract/metadata/07_expiry_semantics.cpp`
- `test/contract/metadata/08_concurrent_create_one_wins.cpp`

These tests must be written so later SQLite/PostgreSQL tasks can reuse them unchanged by providing only backend factory wiring.

## Mandatory Integration Tests

### Storage/service integration
- `test/integration/storage/01_inmemory_resolve_full_flow.cpp`
- `test/integration/storage/02_inmemory_cache_aside.cpp`
- `test/integration/storage/03_inmemory_cache_invalidation_after_update.cpp`
- `test/integration/storage/04_inmemory_cache_invalidation_after_delete.cpp`
- `test/integration/storage/05_inmemory_redirect_visible_states.cpp`
- `test/integration/storage/06_inmemory_analytics_best_effort.cpp`

#### Required integration scenarios
**Correct cases**
- resolve request with cache miss -> repo hit -> cache warm -> redirect returned
- second resolve request with warm cache avoids repo call when possible
- inactive link does not redirect
- expired link does not redirect
- unknown link returns not-found behavior
- analytics event emitted after resolution outcome

**Incorrect / edge cases**
- cache backend failure does not fail redirect when fail-open
- analytics sink failure does not fail redirect
- cache entry invalidated after update and delete
- stale cache data does not override canonical repo state after invalidation path

## Acceptance Checklist
- [ ] Interfaces and shared models exist
- [ ] Service layer depends only on interfaces
- [ ] In-memory metadata/cache/analytics implementations exist
- [ ] Contract suite exists and passes with in-memory factory
- [ ] Integration tests cover redirect-visible semantics and cache invalidation
- [ ] No SQL/Redis/YAML dependencies added

## Exit Criteria
This subtask is complete only when:
1. Core modules contain no backend-native references.
2. In-memory backend passes all listed unit tests, contract tests, and integration tests.
3. Later adapters can be introduced by implementing the same contracts, without refactoring core logic again.
