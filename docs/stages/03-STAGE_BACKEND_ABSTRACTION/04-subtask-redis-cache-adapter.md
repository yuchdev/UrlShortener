# Stage 03 Subtask 04 — Redis Cache Adapter

## Purpose and Scope

This subtask introduces an optional hot-path cache backend for redirect lookup optimization. Redis must be integrated strictly through `ICacheStore`; it must never become the authority for link metadata. This subtask validates cache-aside behavior, deterministic invalidation after metadata mutations, fail-open semantics, and cache serialization compatibility.

### In Scope
- Introduce one Redis client dependency only
- Implement `RedisCacheStore`
- Support `Get`, `Set`, `Delete`, `ClearByPrefix` (or documented safe equivalent)
- Add serialization format with explicit schema/version field
- Add configurable TTL and fail-open/fail-closed behavior
- Add Redis-specific unit and integration tests
- Add service-level integration tests verifying cache-aside correctness

### Out of Scope
- Redis rate limiter (separate subtask)
- Metadata persistence in Redis
- YAML parser except minimal temporary config objects if needed
- analytics backend changes

## Goals
1. Add Redis as a cache only.
2. Keep cache concerns isolated from business logic.
3. Verify stale cache never overrides canonical repository correctness.
4. Validate behavior under Redis outages and TTL expiration.

## Non-Goals
1. Using Redis as primary metadata store.
2. Solving distributed cache invalidation across services beyond this app’s current scope.
3. Adding advanced Redis topologies or clustering support.
4. Implementing rate limiting in this task.

## Dependency Budget

**Allowed new third-party dependency:** `hiredis` only.

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

### Cache behavior rules
- Redirect path must use cache-aside:
  1. read cache
  2. on miss read metadata repo
  3. populate cache
- Cache is optional and fail-open by default
- Create/update/delete/disable/restore paths must write canonical repo first, then invalidate or refresh relevant cache keys deterministically
- Cache value must include at minimum:
  - `target_url`
  - `expires_at`
  - `is_active`
  - optional version/etag
- Serialization format must include explicit version field, e.g. JSON + `schema_version`

### Keying rules
- Key format should be stable, e.g. `link:{short_code}`
- Prefix clearing behavior must be safe and documented
- Environment/service namespace support is desirable if current project conventions use it

### Failure semantics
- Redis unavailable: continue request path when fail-open
- Strict mode/fail-closed may exist if already planned, but default must preserve redirect correctness
- Cache deserialization failure must not crash service

## Suggested Source Layout
- `src/storage/redis/RedisCacheStore.cpp`
- `include/.../storage/redis/RedisCacheStore.hpp`
- `src/storage/redis/RedisCacheSerialization.cpp`
- `test/unit/storage/redis/...`
- `test/integration/redis/...`
- `test/integration/storage/...`

## Mandatory Unit Tests

### Redis adapter unit tests
- `test/unit/storage/redis/01_cache_key_format.cpp`
- `test/unit/storage/redis/02_cache_value_serialization.cpp`
- `test/unit/storage/redis/03_cache_value_deserialization.cpp`
- `test/unit/storage/redis/04_set_get_delete_commands.cpp`
- `test/unit/storage/redis/05_ttl_conversion.cpp`
- `test/unit/storage/redis/06_clear_by_prefix_strategy.cpp`
- `test/unit/storage/redis/07_fail_open_error_mapping.cpp`
- `test/unit/storage/redis/08_schema_version_compatibility.cpp`

#### Required test cases
**Correct cases**
- key builder produces expected key
- value serializes with version field
- deserialization reconstructs full cache value
- TTL conversion preserves intended seconds
- prefix strategy targets expected keys/patterns
- cache command wrappers map success correctly

**Incorrect / edge cases**
- malformed serialized payload fails safely
- unknown schema version handled gracefully
- missing optional fields in payload use documented defaults or fail safely
- zero TTL behavior documented and tested
- network error classification supports fail-open logic
- non-UTF / unusual URL content in payload does not corrupt serialization

## Mandatory Integration Tests

### Redis-specific integration tests
- `test/integration/redis/01_set_get_delete_ttl.py`
- `test/integration/redis/02_unavailable_redis_fail_open.py`
- `test/integration/redis/03_serialization_version_compatibility.py`

### Service-level storage/cache integration tests
- `test/integration/storage/09_cache_aside_with_redis.cpp`
- `test/integration/storage/10_cache_invalidation_after_update_with_redis.cpp`
- `test/integration/storage/11_cache_invalidation_after_delete_with_redis.cpp`
- `test/integration/storage/12_inactive_and_expired_state_not_masked_by_stale_cache.cpp`

#### Required integration scenarios
**Correct cases**
- cache miss populates Redis after repo lookup
- cache hit serves redirect path without repo hit when state is valid
- update invalidates or refreshes relevant key
- delete invalidates relevant key
- inactive and expired states are respected

**Incorrect / edge cases**
- Redis unavailable does not break redirect in fail-open mode
- corrupted Redis payload falls back to repo read
- stale cache value does not survive deterministic invalidation path
- TTL expiry leads to repo reload and cache refresh
- prefix invalidation does not wipe unrelated keys if clear-by-prefix is used

## Acceptance Checklist
- [ ] Redis added as the only new dependency
- [ ] `RedisCacheStore` implemented
- [ ] Versioned serialization implemented
- [ ] Fail-open behavior implemented and tested
- [ ] Cache-aside end-to-end tests exist
- [ ] No business logic depends directly on Redis

## Exit Criteria
This subtask is complete only when:
1. Redis cache behavior is fully exercised by unit and integration tests.
2. Canonical redirect correctness remains intact when Redis is stale, broken, or unavailable.
3. Cache remains a pure optimization boundary behind `ICacheStore`.
