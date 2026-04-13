# SQL Persistence Architecture (SOCI)

This stage introduces a SQL-first metadata persistence family centered on `SqlMetadataRepository`.

## Why SOCI
- Single C++ database access layer that supports SQLite now and PostgreSQL later.
- Keeps repository/service contracts unchanged while enabling backend plug-ins.

## Layering
- Domain/application: `IMetadataRepository` and `RepoError`.
- SQL infrastructure: `SqlConnectionConfig`, `SqlDialect`, `ISqlSessionFactory`, `SqlMetadataRowMapper`, `SqlMetadataRepository`.
- Backend leaf: `SqliteSqlDialect`, `SqliteSessionFactory`, `SqliteErrorMapper`.

## Future PostgreSQL path
PostgreSQL support can be added by implementing dialect/session/error mapper specializations and selecting `SqlBackendKind::postgres`, without changing service-level contracts.
