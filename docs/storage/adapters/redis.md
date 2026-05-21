# Redis Adapter

Purpose: optional cache and optional rate-limit backend.

- Capabilities: cache set/get/delete/ttl, fixed-window rate-limit counters.
- Limitations: non-authoritative for metadata; network dependency.
- Config: `cache.backend=redis` and/or `rate_limit.backend=redis` with `redis_address`.
- Tests: unit `tests/unit/storage/redis/*` and `tests/unit/storage/ratelimiter/*`, integration `tests/integration/redis/*`.
- Failure semantics: cache/rate-limit operations fail-open as covered by tests.
- Use when: lower latency and shared counters are needed.
- Avoid when: strict offline/single-binary operation is required.
