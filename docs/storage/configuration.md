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
  postgres_dsn: postgres://user:${POSTGRES_PASSWORD}@localhost:5432/url_shortener
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

## Selecting the configuration file

The storage configuration file is selected in one of three ways, in
descending priority:

1. **`--storage-config <FILE>`** flag — applies to both the server and
   `link` CLI mode. In `link` mode the flag is stripped before per-verb
   argument parsing, so it cannot collide with any verb flag.
2. **`URL_SHORTENER_STORAGE_CONFIG`** environment variable — read at
   startup before any flag parsing; the flag overrides it when both are
   set. An inherited environment variable silently switches the backend for
   every invocation in that shell session, including one-off commands that
   do not expect persistence (threat model T-41). Set it deliberately and
   unset it when done.
3. **In-memory default** — when neither the flag nor the environment
   variable is set, the built-in in-memory `LegacyLinkStore` is used and no
   YAML file is read.

**Relative paths:** a relative `sqlite_path` is resolved against the process
working directory at startup. When running `link` commands from different
directories, use absolute paths to ensure consistent resolution.

**Bootstrap failure** exits with code **4** in both server and CLI mode. A
fixed, value-free message is printed to stderr — never the DSN, file path, or
raw YAML content. The server binds no port; the CLI writes nothing to stdout.

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
