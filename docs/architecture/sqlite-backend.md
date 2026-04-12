# SQLite Backend (SQL Family)

SQLite is implemented as the first SQL backend through SOCI.

## Schema bootstrap
- SQL lives in `src/storage/sqlite/sql/001_init_links.sql`.
- Bootstrapped lazily on first repository use.

## Serialization
- `attributes_json` stores a JSON payload containing record id and attributes map.
- Timestamps are epoch seconds (`INTEGER`).

## Error mapping
`SqliteErrorMapper` maps backend exceptions to `RepoError` categories, including duplicate key and busy/locked cases.
