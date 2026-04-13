# SQL Persistence Architecture (SOCI)

The metadata persistence stack uses one SQL repository architecture with backend plug-ins.

## Why one cross-database library (SOCI)

- Keep dependency graph simple: one C++ SQL dependency for both SQLite and PostgreSQL.
- Preserve service semantics and repository contracts while swapping SQL backends.
- Avoid splitting application code into backend-specific repository families.

## Layering

- Domain/application: `IMetadataRepository`, `RepoError`, and service logic.
- Shared SQL infrastructure:
  - `SqlConnectionConfig`
  - `SqlBackendKind`
  - `SqlDialect`
  - `ISqlSessionFactory`
  - `SqlMetadataRowMapper`
  - `SqlMetadataRepository`
- Backend plug-ins:
  - SQLite: `SqliteSqlDialect`, `SqliteSessionFactory`, `SqliteErrorMapper`
  - PostgreSQL: `PostgresSqlDialect`, `PostgresSessionFactory`, `PostgresErrorMapper`, `PostgresMigrationRunner`

## Migrations

- SQLite bootstrap remains schema-driven for embedded development.
- PostgreSQL migrations are versioned assets in `db/migrations/postgres` with explicit `up`/`down` files.
- Ordering is deterministic by migration version prefix.

## PostgreSQL config and retry behavior

`SqlConnectionConfig` includes PostgreSQL options:
- `dsn`
- `connect_timeout`
- `statement_timeout`
- optional `application_name`
- bounded `max_retries`

Retry logic is constrained to transient/timeout failures and bounded by `max_retries`.
