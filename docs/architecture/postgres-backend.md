# PostgreSQL Backend (SOCI)

PostgreSQL is enabled as a second SQL backend in the same SQL persistence architecture used by SQLite.

## Components

- Common SQL infrastructure: `SqlMetadataRepository`, `SqlDialect`, `ISqlSessionFactory`, `SqlMetadataRowMapper`.
- PostgreSQL plug-ins:
  - `PostgresSqlDialect`
  - `PostgresSessionFactory`
  - `PostgresErrorMapper`
  - `PostgresMigrationRunner`

## Schema and migrations

Migration assets are versioned under `db/migrations/postgres` with explicit up/down scripts.
`attributes_json` is stored as `JSONB`, and timestamps use `TIMESTAMPTZ`.

## Error mapping

`PostgresErrorMapper` maps SQLSTATE/diagnostic text into repository taxonomy:
- unique violation -> `already_exists`
- serialization/deadlock/connection interruptions -> `transient_failure`
- statement timeout/cancel -> `timeout`
- auth/config/schema errors -> `permanent_failure`
