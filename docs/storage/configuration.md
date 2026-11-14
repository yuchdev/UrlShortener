# Storage Configuration

Defaults: `cache.backend=none`, `analytics.backend=noop`, `rate_limit.enabled=false`.

## In-memory metadata + no cache

```yaml
metadata:
  backend: inmemory
```

## SQLite metadata + in-memory cache

```yaml
metadata:
  backend: sqlite
  sqlite_path: ./data/shortener.sqlite3
cache:
  backend: inmemory
```

## PostgreSQL metadata + Redis cache + Redis rate limiting

```yaml
metadata:
  backend: postgres
  postgres_dsn: postgres://user:password@localhost:5432/url_shortener
cache:
  backend: redis
  redis_address: 127.0.0.1:6379
  default_ttl_seconds: 300
analytics:
  backend: inmemory
rate_limit:
  enabled: true
  backend: redis
  redis_address: 127.0.0.1:6379
```

Required fields:

- `metadata.sqlite_path` when metadata backend is `sqlite`
- `metadata.postgres_dsn` when metadata backend is `postgres`
- `cache.redis_address` when cache backend is `redis`
- `rate_limit.redis_address` when rate-limit backend is `redis`

Startup fails on invalid config, e.g. unknown backend names or missing required backend fields.

## File permissions and secrets

The storage config YAML holds backend credentials (`postgres_dsn`,
`redis_address`) and the SQLite database holds every target URL plus packed
tags/metadata. On a shared host both must be protected (threat model T-30):

- Create both the YAML and the SQLite database file as mode **0600**, owned by
  the service account. When the bootstrap creates a *new* SQLite file it runs
  the first open under a restrictive umask (0077) on POSIX, so the database file
  and its `-journal`/`-wal`/`-shm` companions are born owner-only (0600) with no
  world/group-readable window, and additionally chmods the main file to 0600 as
  defense in depth. It never widens the permissions of a pre-existing file, so
  an operator-chosen mode is preserved. (The umask change is process-wide but
  applied only at single-threaded startup and restored immediately, so it does
  not affect files created while serving.)
- The `--storage-config` path is visible in process listings. For
  secrets-laden configs prefer the `URL_SHORTENER_STORAGE_CONFIG` environment
  variable (flag takes precedence over env, env over the in-memory default).
- Startup never logs the DSN, Redis address, or SQLite path; a parse or probe
  failure is reported with a fixed, value-free message.
