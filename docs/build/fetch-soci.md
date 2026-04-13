# Fetching SOCI via CMake

SOCI is fetched during configure using `FetchContent` in `cmake/FetchSOCI.cmake`.

## Stage constraints
- SQLite backend: ON
- PostgreSQL backend: OFF
- Other SOCI backends: OFF

Configuration fails if required SOCI SQLite targets are missing or PostgreSQL is accidentally enabled.
