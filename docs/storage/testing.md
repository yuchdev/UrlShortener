# Storage Testing Strategy

- **Unit tests** validate adapter internals and error mapping.
- **Contract tests** validate common metadata behavior reused across backends.
- **Integration tests** validate backend services and CI orchestration.

Contract reuse: `tests/contract/metadata/*` is executed by in-memory, SQLite, and Postgres runners.

Local commands:

- `ctest -L unit --output-on-failure`
- `ctest -L contract --output-on-failure`
- `ctest -L integration --output-on-failure`

External services for full run:

- PostgreSQL for psql integration suite
- Redis for cache/rate-limit integration suite
