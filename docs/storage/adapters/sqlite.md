# SQLite Adapter

Purpose: local/file-backed SQL metadata adapter.

- Capabilities: full metadata contract, schema bootstrap, migrations.
- Limitations: single-file DB contention under heavy concurrency.
- Config: `metadata.backend=sqlite`, `metadata.sqlite_path` required.
- Tests: unit `tests/unit/storage/sqlite/*`, integration `tests/integration/sqlite/*`.
- Failure semantics: SQL errors mapped to repository taxonomy.
- Use when: local dev/single-node deployments.
- Avoid when: multi-node/high write concurrency.
