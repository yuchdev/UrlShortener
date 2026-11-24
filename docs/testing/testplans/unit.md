# Unit Test Plan

## Scope

Unit tests verify isolated behavior with deterministic inputs and no external
services. They are the first required test layer for every code change.

## Locations and label

| Item | Value |
|---|---|
| Source root | `tests/unit/` |
| CTest label | `unit` |
| Framework | Boost.Test |

## Required coverage

- Core URL, slug, timestamp, status, and redirect helpers.
- Shared `url_shortener::app` command-layer services with fake ports.
- CLI parser and command descriptor mapping.
- HTTP route registry, router matching, route context, and handler mapping.
- Storage model validation, SQL dialects, cache behavior, and error mapping.
- Analytics builders, sanitizers, queue, worker, retention, and stats mapping.
- Security primitives, auth broker behavior, access guards, and log redaction.
- Command registry pipeline (`commands__01`..`14`), error table, ArgSpec, audit,
  and catalog; link/browse/job command implementations (`commands__20`..`79`).
- TUI models, goldens, flows, and conformance shim (`tui__01`..`tui__37`);
  headless render harness; `UPDATE_GOLDEN=1` for intentional layout changes.
- Conformance corpus lint (`conformance_00_corpus_lint`) and matcher vectors
  (`conformance_01_matcher_vectors`), which carry the `unit` label.

## Acceptance criteria

- Tests do not open sockets or require real databases.
- Time-sensitive behavior uses deterministic clocks where supported.
- Assertions check exact enum values, status codes, JSON fields, or error codes.
- New common C++ sources are registered before tests depend on them.

## Command

```powershell
ctest --test-dir cmake-build -C Debug -L unit --output-on-failure
```
