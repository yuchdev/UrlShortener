# Storage configuration

Defaults: metadata=inmemory, cache=none, analytics=noop, rate_limit.enabled=false.

```yaml
metadata:
  backend: sqlite
  sqlite_path: /tmp/links.db
cache:
  backend: redis
  redis_address: 127.0.0.1:6379
  default_ttl_seconds: 60
analytics:
  backend: inmemory
rate_limit:
  enabled: true
  backend: redis
  redis_address: 127.0.0.1:6379
```
