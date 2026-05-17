# Backend Selection Guide

## Supported backends and maturity

| Backend | Role | Maturity | Best fit |
|---|---|---|---|
| In-memory | metadata/cache/rate-limit | dev/test | local development, smoke tests |
| SQLite | metadata + analytics events | production-lite | single-node or low-complexity deployments |
| PostgreSQL | metadata + analytics events | production | multi-node durability and richer operations |
| Redis | cache + rate limiting | production adjunct | latency-sensitive redirect path |

## Tradeoffs

- **In-memory**: fastest setup, no durability.
- **SQLite**: simple operations, local disk dependency.
- **PostgreSQL**: strongest durability and concurrency, higher ops overhead.
- **Redis**: excellent cache/rate-limit latency, additional infrastructure.

## Failure modes and complexity

- In-memory restart causes state loss.
- SQLite can encounter file lock pressure under contention.
- PostgreSQL failures surface as transient backend errors; use retries/backoff.
- Redis outages should fail-open where configured for availability.

## Migration considerations

- Keep schema migrations versioned and rehearsed.
- Use dual-read/dual-write only if required; otherwise prefer maintenance window + verification.
- Validate route behavior and error budgets after backend changes.
