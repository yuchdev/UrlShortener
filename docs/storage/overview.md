# Storage Overview

Stage 03 uses a ports-and-adapters design: core/application depends only on storage interfaces (`IMetadataRepository`, `ICacheStore`, `IRateLimiter`, `IAnalyticsSink`), while infrastructure provides concrete adapters. PostgreSQL/SQLite/Redis/in-memory details stay under `src/storage/*`.

## Boundaries
- **Core/Application**: link behavior and business rules.
- **Infrastructure**: SQL dialect/session factories, Redis cache/rate limiter, in-memory adapters.

## Authoritative Store
PostgreSQL (or SQLite in local mode) is authoritative for link metadata. Redis is non-authoritative and only used for cache/rate-limit acceleration.

## Cache-aside
1. Read attempts cache first.
2. On miss/failure, read metadata repository.
3. Populate cache best-effort.
4. Update/delete invalidates cache keys best-effort.

## Failure semantics summary
- Metadata failures: fail request with mapped repository error.
- Cache failures: fail-open; metadata path still executes.
- Rate-limit backend failures: fail-open by policy in existing tests.
