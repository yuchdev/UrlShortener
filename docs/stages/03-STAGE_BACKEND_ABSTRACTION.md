# Stage 03 Technical Specification

## Introduce Storage Abstraction and Multiple Backends

## 1. Purpose and Scope

This specification defines the Stage 03 architecture and implementation plan for introducing a storage abstraction layer that supports multiple interchangeable backends for metadata and cache while keeping core service logic backend-agnostic. Analytics forwarding remains optional and must not compete with the Stage 04 analytics repository as the source of truth.

The stage covers:
- repository interfaces for link metadata
- cache abstraction
- optional analytics forwarding abstraction (non-authoritative; Stage 04 repository remains authoritative)
- in-memory backend (tests/dev)
- SQLite backend (development)
- PostgreSQL backend (durable/production metadata)
- Redis adapter (hot lookup cache, optional rate-limit primitives)
- configuration-driven backend selection
- reusable contract tests across backends
- extension path for additional adapters

Out of scope:
- changing business rules for link creation/redirect semantics
- adding non-storage product features
- introducing distributed transactions across backends

---

## 2. Goals and Non-Goals

### 2.1 Goals
1. Core logic (HTTP handlers/services/use-cases) MUST depend only on storage interfaces.
2. Metadata persistence MUST support at minimum InMemory and PostgreSQL in a functional manner.
3. Cache integration MUST be optional and MUST NOT leak cache-specific concerns into business logic.
4. Backend selection MUST be configuration-driven (no compile-time branching in business layer).
5. Contract tests MUST validate consistent behavior across metadata backends. Service-level contract tests MUST also verify redirect-visible semantics across backends (active/disabled/expired/deleted, redirect type persistence, cache invalidation after updates).
6. System MUST remain open for extension with additional adapters.

### 2.2 Non-Goals
1. Full ORM adoption.
2. Multi-primary replication management.
3. Cross-backend two-phase commit.
4. Introducing analytics dashboards or a parallel authoritative analytics persistence model.

---

## 3. Target Architecture

Adopt a ports-and-adapters (hexagonal) layout:

- **Domain/Core layer**
  - Link service use-cases and business invariants.
  - Depends only on interfaces (`IMetadataRepository`, `ICacheStore`, `IAnalyticsSink`, `IRateLimiter` optional).

- **Application layer**
  - Orchestrates calls to repository/cache/analytics.
  - Cache-aside flow and fallbacks.

- **Infrastructure layer**
  - Concrete adapters:
    - InMemory metadata/cache/analytics
    - SQLite metadata
    - PostgreSQL metadata
    - Redis cache (+ optional rate-limit primitives)
  - Configuration parsing and adapter factory/wiring.


## 3.2 Backend capability tiers

Backends are not equal and documentation/configuration MUST say so explicitly.

- **InMemory**: tests and local smoke runs only; never authoritative for production data.
- **SQLite**: local development, debugging, and optionally very small single-node environments; not the primary production recommendation.
- **PostgreSQL**: canonical production metadata source of truth.
- **Redis**: cache and optional rate-limit helper only; never the authority for link metadata.

All examples and deployment guidance should treat PostgreSQL as the default production metadata backend.

### 3.1 High-level interaction

1. Request arrives to resolve short code.
2. Service checks cache interface.
3. On cache miss, service loads metadata via repository interface.
4. Service populates cache via cache interface.
5. Service may optionally forward analytics via a best-effort sink interface, but redirect correctness and authoritative analytics storage remain outside this stage.

---

## 4. Interface Specifications (Ports)

> Naming shown in C++-style for this codebase; exact naming can follow project conventions.

## 4.1 Metadata Repository Port

### 4.1.1 Entity model (minimum)
`LinkRecord`
- `short_code: string` (unique)
- `target_url: string`
- `created_at: timestamp`
- `updated_at: timestamp`
- `expires_at: optional<timestamp>`
- `is_active: bool`
- `owner_id: optional<string>`
- `attributes: map<string,string>` (optional/extensible)

### 4.1.2 Interface
```cpp
class IMetadataRepository {
public:
  virtual ~IMetadataRepository() = default;

  virtual Result<LinkRecord, RepoError> CreateLink(const CreateLinkRequest&) = 0;
  virtual Result<std::optional<LinkRecord>, RepoError> GetByShortCode(std::string_view short_code) = 0;
  virtual Result<void, RepoError> UpdateLink(const LinkRecord&) = 0;
  virtual Result<void, RepoError> DeleteLink(std::string_view short_code) = 0;

  virtual Result<std::vector<LinkRecord>, RepoError> ListLinks(const ListLinksQuery&) = 0;
  virtual Result<bool, RepoError> Exists(std::string_view short_code) = 0;
};
```

### 4.1.3 Error taxonomy
`RepoError` SHOULD include:
- `NotFound`
- `AlreadyExists`
- `ValidationFailed`
- `Conflict` (optimistic lock/version conflict if introduced)
- `TransientFailure`
- `PermanentFailure`
- `Timeout`

Adapters MUST map backend-native errors into this taxonomy.

## 4.2 Cache Port

### 4.2.1 Interface
```cpp
class ICacheStore {
public:
  virtual ~ICacheStore() = default;

  virtual Result<std::optional<CacheValue>, CacheError> Get(std::string_view key) = 0;
  virtual Result<void, CacheError> Set(
      std::string_view key,
      const CacheValue& value,
      std::chrono::seconds ttl) = 0;
  virtual Result<void, CacheError> Delete(std::string_view key) = 0;
  virtual Result<void, CacheError> ClearByPrefix(std::string_view prefix) = 0;
};
```

`CacheValue` for redirect hot path SHOULD at least include `target_url`, `expires_at`, `is_active`, and optional `etag/version`.

### 4.2.2 Cache policy
- Read path uses cache-aside.
- Redirect reads MUST treat cache as an optimization only.
- Create path: write canonical repository first, then optionally warm cache.
- Update/delete/enable/disable/restore path: write canonical repository first, then invalidate cache key deterministically.
- Cache entries SHOULD carry an `etag`/`version` when practical so stale entries can be detected or safely replaced.
- Cache failures MUST NOT fail core redirect operation unless strict mode is enabled.

## 4.3 Optional Analytics Forwarder Port

### 4.3.1 Event model
`LinkAccessEvent`
- `short_code`
- `resolved_target`
- `timestamp`
- `request_id`
- `client_ip_hash` (no raw PII by default)
- `user_agent`
- `referrer`
- `outcome` (hit/miss/expired/inactive/not_found)

### 4.3.2 Interface
```cpp
class IAnalyticsSink {
public:
  virtual ~IAnalyticsSink() = default;

  virtual Result<void, AnalyticsError> Emit(const LinkAccessEvent&) = 0;
  virtual Result<void, AnalyticsError> Flush() = 0; // optional no-op
};
```

Analytics forwarding, if present in this stage, SHOULD be best-effort by default with bounded buffering and MUST be documented as non-authoritative. Stage 04 internal analytics repository remains the source of truth for aggregate analytics.

## 4.4 Optional Rate Limiter Port

Redis rate-limit primitives are optional. Expose separate interface to avoid contaminating cache abstraction:

```cpp
class IRateLimiter {
public:
  virtual ~IRateLimiter() = default;
  virtual Result<RateLimitDecision, RateLimitError> Allow(
      std::string_view key,
      uint32_t limit,
      std::chrono::seconds window) = 0;
};
```

---

## 5. Adapter Implementations

## 5.1 InMemory Adapter (dev/tests)

Components:
- `InMemoryMetadataRepository`
- `InMemoryCacheStore`
- `InMemoryAnalyticsSink`
- optional `InMemoryRateLimiter`

Characteristics:
- Thread-safe map(s) with mutex/RW lock.
- Deterministic clock injection for testability.
- TTL handling via lazy eviction on read + periodic cleanup optional.

Use cases:
- unit tests
- local smoke runs
- CI fast tests

## 5.2 SQLite Metadata Adapter (development stage)

Purpose:
- realistic SQL backend for local development.

Schema (minimum):
`links` table
- `short_code TEXT PRIMARY KEY`
- `target_url TEXT NOT NULL`
- `created_at INTEGER NOT NULL`
- `updated_at INTEGER NOT NULL`
- `expires_at INTEGER NULL`
- `is_active INTEGER NOT NULL`
- `owner_id TEXT NULL`
- `attributes_json TEXT NULL`

Requirements:
- parameterized queries only
- unique constraint behavior mapped to `AlreadyExists`
- busy timeout configured
- migration bootstrap on startup (if enabled)

## 5.3 PostgreSQL Metadata Adapter (production-grade)

Purpose:
- durable backend meeting exit criteria.

Schema (minimum):
`links` table
- `short_code VARCHAR PRIMARY KEY`
- `target_url TEXT NOT NULL`
- `created_at TIMESTAMPTZ NOT NULL DEFAULT now()`
- `updated_at TIMESTAMPTZ NOT NULL DEFAULT now()`
- `expires_at TIMESTAMPTZ NULL`
- `is_active BOOLEAN NOT NULL DEFAULT TRUE`
- `owner_id VARCHAR NULL`
- `attributes_json JSONB NULL`

Indexes:
- PK on `short_code`
- optional index on `owner_id`
- partial index on active/unexpired lookups if needed for list/query patterns

Operational requirements:
- connection pooling
- prepared statements
- retry policy for transient errors (bounded, idempotent-safe)
- migrations versioned and reversible

## 5.4 Redis Cache Adapter

Purpose:
- hot lookup cache and optional rate limiting.

Cache key patterns:
- `link:{short_code}` -> serialized `CacheValue`
- optional namespacing by environment/service

Requirements:
- TTL set per key via configuration default + override
- serialization format versioning (e.g., JSON with `schema_version`)
- timeout and retry policy for Redis network failures
- fail-open behavior configurable

Optional rate limiting:
- token bucket or fixed window via Lua script/atomic operations
- key format: `ratelimit:{scope}:{id}`
- isolate from generic cache port using `IRateLimiter`

---

## 6. Configuration-Driven Backend Selection

Backend selection MUST be runtime-configurable.

Example YAML:
```yaml
storage:
  metadata:
    backend: postgres # inmemory | sqlite | postgres
    sqlite:
      path: ./data/dev.db
      busy_timeout_ms: 2000
      auto_migrate: true
    postgres:
      dsn: postgres://user:pass@localhost:5432/app
      max_pool_size: 20
      connect_timeout_ms: 3000
      statement_timeout_ms: 2000
      auto_migrate: false

  cache:
    backend: redis # none | inmemory | redis
    default_ttl_seconds: 300
    fail_open: true
    redis:
      address: 127.0.0.1:6379
      db: 0
      password: ""
      connect_timeout_ms: 500
      command_timeout_ms: 200

  analytics:
    backend: noop # noop | inmemory | stdout | postgres (future)
    async: true
    queue_capacity: 10000

  rate_limit:
    enabled: false
    backend: redis # redis | inmemory
```

Validation rules:
- invalid backend name => startup failure with clear diagnostics
- backend-specific required fields must be enforced
- default values documented and deterministic

---

## 7. Business Logic Integration Rules

1. Core services receive interfaces via DI/composition root.
2. No backend-specific headers/types referenced in core modules.
3. Cache and analytics interactions are side concerns:
   - cache read before repo read on resolve path
   - analytics emission after resolution outcome
4. Failover semantics:
   - repo failure: request fails
   - cache failure: continue (unless strict mode)
   - analytics failure: log + continue

Pseudo-flow (resolve):
```
cache.get(code)
  hit => validate => analytics.emit(hit) => return
  miss => repo.get(code)
          not_found => analytics.emit(not_found) => return 404
          found => validate => cache.set(code, value, ttl) => analytics.emit(hit) => return
```

---

## 8. Testing Strategy

## 8.1 Contract Tests (Reusable Across Metadata Backends)

Create shared contract suite parameterized by factory:
- `CreateAndGet_RoundTrip`
- `Create_DuplicateShortCode_ReturnsAlreadyExists`
- `Get_Unknown_ReturnsEmpty`
- `Update_Existing_Persists`
- `Delete_RemovesRecord`
- `List_FilteringAndPagination`
- `ExpirySemantics_Consistent`
- `ConcurrentCreate_OneWinsOthersConflict`

Backends under contract:
- in-memory metadata
- SQLite metadata
- PostgreSQL metadata

Contract suite MUST be run identically for each backend.

## 8.2 Backend-Specific Tests

### PostgreSQL
- migration up/down smoke
- connection loss + transient retry behavior
- JSONB attributes round-trip

### SQLite
- DB file initialization
- busy/lock behavior mapping

### Redis cache
- set/get/delete TTL behavior
- unavailable Redis fail-open/fail-closed behavior
- serialization version compatibility

## 8.3 Integration Tests

- service-level tests proving backend agnosticism by swapping only configuration.
- at least two full-path integration runs:
  - metadata=inmemory, cache=inmemory
  - metadata=postgres, cache=redis (or none when unavailable)

---

## 9. Migration and Rollout Plan

1. Introduce interfaces and adapt existing logic to depend on interfaces.
2. Implement in-memory adapters and wire as default.
3. Add SQLite metadata adapter for local developer workflow.
4. Add PostgreSQL adapter + migrations.
5. Add Redis cache adapter and optional rate limiter.
6. Add configuration parser/factory selection.
7. Add contract tests and backend-specific integration tests.
8. Update docs and examples.
9. Enable staged rollout with feature flags / config toggles.

Rollback strategy:
- switch config to previously stable backend (inmemory/sqlite) for non-prod.
- preserve old schema migration compatibility during initial deployment window.

---

## 10. Observability and Operations

Metrics (minimum):
- repository latency/error count by backend + operation
- cache hit/miss ratio
- cache operation failures
- analytics enqueue/drop counts
- rate-limit allow/deny counts

Logging:
- structured logs with backend identifiers and error categories
- redact secrets and sensitive payloads

Health checks:
- readiness SHOULD verify active backend connectivity (with timeout budget)
- liveness MUST avoid deep dependency checks

---

## 11. Security and Reliability Considerations

- Use parameterized SQL queries (SQL injection prevention).
- Encrypt transport for PostgreSQL/Redis where applicable.
- Avoid storing raw personal data in analytics events by default.
- Enforce bounded queues and timeouts to avoid resource exhaustion.
- Ensure adapters are thread-safe under server concurrency model.

---

## 12. Documentation Deliverables

Required docs for Stage 03:
1. **Storage abstraction layer overview** (architecture + dependency boundaries).
2. **Backend configuration guide** with examples for in-memory, SQLite, PostgreSQL, Redis.
3. **Backend-specific notes** (operational limits, tuning guidance).
4. **How to add a new adapter** (detailed checklist):
   - implement interface(s)
   - map errors to shared taxonomy
   - register in factory/config parser
   - add contract test wiring
   - add adapter-specific tests
   - document configuration and failure semantics

Suggested file set:
- `docs/storage/overview.md`
- `docs/storage/configuration.md`
- `docs/storage/adapters/postgres.md`
- `docs/storage/adapters/sqlite.md`
- `docs/storage/adapters/redis.md`
- `docs/storage/how-to-add-adapter.md`

---

## 13. Exit Criteria Mapping

### Criterion: core service logic is backend-agnostic
Satisfied when:
- core modules include only interface dependencies
- integration test demonstrates swapping backend via config only

### Criterion: at least in-memory + PostgreSQL are functional
Satisfied when:
- contract tests pass for both
- integration tests pass with each

### Criterion: cache integration works without contaminating business logic
Satisfied when:
- cache used only via `ICacheStore` in service layer
- disabling cache requires no core code change
- fail-open semantics validated in tests

---

## 14. Acceptance Checklist

- [ ] Interfaces added for metadata, cache, analytics (and optional rate limit)
- [ ] In-memory adapters implemented
- [ ] SQLite metadata adapter implemented
- [ ] PostgreSQL metadata adapter implemented
- [ ] Redis cache adapter implemented
- [ ] Config-driven adapter factory implemented
- [ ] Contract tests reusable across metadata backends
- [ ] Backend-specific tests present
- [ ] Documentation completed including "add new adapter" guide
- [ ] Exit criteria validated in CI/test evidence

