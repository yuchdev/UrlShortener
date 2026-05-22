# PostgreSQL Adapter

Purpose: authoritative production-grade metadata store.

- Capabilities: full metadata contract, migration runner, retry-aware session factory.
- Limitations: requires managed Postgres lifecycle and network availability.
- Config: `metadata.backend=postgres`, `metadata.postgres_dsn` required.
- Tests: unit `tests/unit/storage/psql/*`, integration `tests/integration/psql/*`.
- Failure semantics: transient/permanent mapping via postgres error mapper.
- Use when: production durability/consistency is required.
- Avoid when: zero external dependencies are required.
