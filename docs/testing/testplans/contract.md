# Mock and Contract Test Plan

## Scope

Contract tests define backend behavior once and run it independently of any
single storage implementation. This is the project mock/fake layer for storage
semantics.

## Locations and label

| Item | Value |
|---|---|
| Source root | `tests/contract/metadata/` |
| CTest label | `contract` |
| Framework | Boost.Test |

## Required coverage

- Create/get roundtrip.
- Duplicate short-code handling.
- Unknown record lookup.
- Update and delete semantics.
- List filtering and pagination.
- Expiry visibility.
- Concurrent create conflict behavior.

## Acceptance criteria

- New storage behavior is added here before backend-specific assertions.
- SQLite and PostgreSQL integration runners continue to satisfy the same
  contract suite.
- Contract failures are treated as backend correctness failures, not test-only
  differences.

## Command

```powershell
ctest --test-dir cmake-build -C Debug -L contract --output-on-failure
```
