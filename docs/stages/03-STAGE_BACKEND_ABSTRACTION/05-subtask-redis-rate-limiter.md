# Stage 03 Subtask 05 — Optional Redis Rate Limiter

## Purpose and Scope

This subtask introduces optional rate limiting using Redis primitives while keeping it fully separate from generic cache concerns. Even though Redis is reused as the backend technology, the abstraction must be independent: `IRateLimiter` must not be implemented by abusing or expanding `ICacheStore`.

### In Scope
- Reuse existing Redis dependency from Subtask 04
- Implement `RedisRateLimiter`
- Support deterministic `Allow(key, limit, window)` behavior
- Use atomic Redis operation or Lua script
- Add unit and integration tests for concurrency, window reset, and boundary conditions
- Keep rate limiting optional and independently wired

### Out of Scope
- New Redis dependency
- generalized abuse-prevention analytics
- advanced rate limiting algorithms beyond the chosen documented implementation
- changes to core metadata/cache semantics

## Goals
1. Introduce rate limiting without contaminating cache abstraction.
2. Guarantee deterministic behavior under concurrent requests.
3. Verify boundary conditions, window reset, and configuration edge cases.
4. Keep the feature optional.

## Non-Goals
1. Full traffic-shaping framework.
2. User/account policy design beyond technical implementation.
3. Multi-region distributed consistency guarantees.
4. New backend types for rate limiting in this task.

## Dependency Budget

**Allowed new third-party dependencies:** none. Reuse Redis client added previously.

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

### Required abstraction rule
- `IRateLimiter` remains a separate interface
- No additional methods may be added to `ICacheStore` to support rate limiting
- Cache and rate limiting must be instantiable independently

### Algorithm requirement
Use one clearly documented algorithm:
- fixed window, or
- token bucket

The implementation must document:
- allow/deny boundary behavior
- counter reset semantics
- expected Redis key format
- atomicity mechanism

### Keying
- Key format must be stable, e.g. `ratelimit:{scope}:{id}`
- Scope naming must be documented

## Suggested Source Layout
- `src/storage/redis/RedisRateLimiter.cpp`
- `include/.../storage/redis/RedisRateLimiter.hpp`
- `src/storage/redis/lua/ratelimit.lua` (if Lua chosen)
- `test/unit/storage/ratelimiter/...`
- `test/integration/redis/...`

## Mandatory Unit Tests
- `test/unit/storage/ratelimiter/01_allow_under_limit.cpp`
- `test/unit/storage/ratelimiter/02_deny_over_limit.cpp`
- `test/unit/storage/ratelimiter/03_boundary_at_exact_limit.cpp`
- `test/unit/storage/ratelimiter/04_window_reset.cpp`
- `test/unit/storage/ratelimiter/05_key_format.cpp`
- `test/unit/storage/ratelimiter/06_error_mapping.cpp`
- `test/unit/storage/ratelimiter/07_invalid_configuration.cpp`

#### Required test cases
**Correct cases**
- requests below limit are allowed
- exact boundary behavior matches documented policy
- first request in new window succeeds
- distinct keys do not interfere
- generated Redis keys are stable

**Incorrect / edge cases**
- limit zero behavior documented and tested
- empty key behavior documented and tested
- invalid window size rejected
- Redis command/script failure maps to correct rate limit error
- large counters/window values do not overflow
- concurrency model assumptions are explicit

## Mandatory Integration Tests
- `test/integration/redis/04_ratelimiter_fixed_window.py`
- `test/integration/redis/05_ratelimiter_concurrency.py`
- `test/integration/storage/13_rate_limit_service_integration.cpp`

#### Required integration scenarios
**Correct cases**
- real Redis backend enforces allow/deny over a live window
- concurrent requests produce deterministic total allowance
- service integration can consume rate limit decision without backend leakage

**Incorrect / edge cases**
- Redis unavailable behavior is documented and tested
- misconfiguration fails early
- reset after window occurs as documented
- unrelated scopes remain isolated

## Acceptance Checklist
- [ ] `RedisRateLimiter` implemented separately from cache
- [ ] Atomic/window behavior documented
- [ ] Boundary and concurrency tests exist
- [ ] Service integration path exists if rate limiting is already consumed by app flow

## Exit Criteria
This subtask is complete only when:
1. Rate limiting is technically correct and independently abstracted.
2. No cache interface pollution occurred.
3. Live Redis integration validates concurrency and boundary behavior.
