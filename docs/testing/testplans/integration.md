# Integration Test Plan

## Scope

Integration tests verify behavior across multiple real components: process
execution, storage adapters, cache behavior, analytics repositories, and
HTTP/CLI flows.

## Locations and label

| Item | Value |
|---|---|
| Source root | `tests/integration/` |
| CTest label | `integration` |
| Framework | Boost.Test and Python standard library |

## Required coverage

- HTTP management and redirect flows through the built binary.
- CLI command flows through the built binary.
- Cache-aside and cache invalidation.
- Config-driven backend selection.
- SQLite/PostgreSQL/Redis adapter behavior where available.
- Analytics event persistence, aggregation, retention, disabled mode, and
  failure isolation.

## CLI integration expectations

CLI tests run the binary from `SIMPLE_HTTP_BIN`, use a temporary working
directory, and assert:

- command exit code;
- stdout JSON shape and values;
- stderr diagnostics for invalid inputs;
- state changes visible through follow-up CLI commands;
- CLI mode exits without starting the HTTP server.

## Command

```powershell
ctest --test-dir cmake-build -C Debug -L integration --output-on-failure
```
