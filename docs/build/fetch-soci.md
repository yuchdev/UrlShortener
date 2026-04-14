# Fetching SOCI (SQLite + PostgreSQL)

The build fetches a pinned SOCI revision via CMake FetchContent (`v4.0.3`).

Enabled backends:
- SQLite (`SOCI_SQLITE3=ON`)
- PostgreSQL (`SOCI_POSTGRESQL=ON`)

Disabled backends:
- DB2, Firebird, MySQL, ODBC, Oracle

The project fails configuration if either SQLite or PostgreSQL backend targets are missing.
