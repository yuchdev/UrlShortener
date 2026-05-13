# SOCI Fetch Configuration (SQLite + PostgreSQL)

The project uses one SQL access library, [SOCI](https://github.com/SOCI/soci), for both SQL backends.

## Why this setup exists

- Keep one SQL persistence architecture for `SqlMetadataRepository`.
- Avoid introducing a second DB access stack (`libpqxx`, ORM wrappers, or raw backend-specific application wiring).
- Ensure SQLite and PostgreSQL stay aligned to one repository contract.

## Pinned SOCI configuration

SOCI is fetched in `cmake/FetchSOCI.cmake` using a pinned tag (`v4.0.3`) and configured with:

- enabled: SQLite3 backend
- enabled: PostgreSQL backend
- disabled: DB2, Firebird, MySQL, ODBC, Oracle

The CMake helper exports stable aliases used by the app and tests:

- `SOCI::soci_core`
- `SOCI::soci_sqlite3`
- `SOCI::soci_postgresql`

Configuration fails loudly if any required SOCI target is missing.

## Local prerequisites

PostgreSQL client development files must be installed so CMake can resolve `find_package(PostgreSQL REQUIRED)`.

Typical Ubuntu packages:

```bash
sudo apt-get update
sudo apt-get install -y libpq-dev postgresql-client
```

SQLite development headers are also required:

```bash
sudo apt-get install -y libsqlite3-dev
```

## CI/build verification

Use these checks to validate build wiring:

- `test/integration/build/02_sqlite_backend_enabled.py`
- `test/integration/build/03_fetch_soci_sqlite_and_postgres.py`
- `test/integration/build/04_postgres_backend_enabled.py`

These tests assert that:

- SOCI was fetched/configured,
- SQLite and PostgreSQL backends are both enabled,
- unrelated backends remain disabled,
- PostgreSQL discovery variables are present in the configured cache.
