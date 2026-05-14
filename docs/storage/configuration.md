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
