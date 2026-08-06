# End-to-End Test Plan

## Scope

E2E tests are black-box executable analogs of manual QA scenarios. Each manual
scenario must have a matching script in `tests/e2e/scripts/sections/`.

## Locations and label

| Item | Value |
|---|---|
| Manual specs | `docs/testing/e2e_testing/sections/` |
| Scripts | `tests/e2e/scripts/sections/` |
| CTest label | `e2e` |
| Framework | Bash and Python helper scripts |

## Required coverage

- Service lifecycle and readiness.
- REST create/read/redirect/error flows.
- Expiration, analytics, auth, permissions, admin, and concurrency behavior.
- CLI create/get/stat command flows.
- CLI/server separation: command mode must not open the default HTTP port.

## Acceptance criteria

- Every manual section spec maps to an executable section script.
- Sections are isolated and do not depend on shared state from previous runs.
- Runtime state and persisted state are asserted explicitly.
- Linux/POSIX-only requirements are documented.

## Commands

```bash
bash tests/e2e/scripts/run_section.sh 11_cli_link_create
ctest --test-dir build -L e2e --output-on-failure
```
