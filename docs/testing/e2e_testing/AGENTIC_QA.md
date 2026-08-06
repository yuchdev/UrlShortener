# End-to-End Test Plan

## Purpose

The e2e suite is the executable analog for manual QA scenarios. Each scenario
is documented in `docs/testing/e2e_testing/sections/` and implemented by a
matching script in `tests/e2e/scripts/sections/`.

The suite covers both application modes:

- **Service scenarios** start a test service, execute REST-like operations, and
  assert runtime and persisted state.
- **CLI scenarios** execute the built `url_shortener` binary as a one-shot
  command, assert console output and state, and verify the HTTP server is not
  started.

## Architecture

| Concern | Location |
|---|---|
| Manual section specs | `docs/testing/e2e_testing/sections/*.md` |
| Executable section scripts | `tests/e2e/scripts/sections/*.sh` |
| Section runner | `tests/e2e/scripts/run_section.sh` |
| Multi-section runner | `tests/e2e/scripts/run_all_sections.sh` |
| Expected state snapshots | `tests/e2e/expected/*.json` |
| Runtime/database assertion helper | `tools/sqlite_state_assert.py` |
| Failure artifacts | `tests/e2e/failures/` |
| Runtime scratch | `tests/e2e/tmp/` |

## Scenario catalogue

| Section | Manual spec | Executable analog | Category |
|---|---|---|---|
| 01 | `sections/01_server_boot.md` | `tests/e2e/scripts/sections/01_server_boot.sh` | Service lifecycle |
| 02 | `sections/02_create_short_url.md` | `tests/e2e/scripts/sections/02_create_short_url.sh` | REST create |
| 03 | `sections/03_redirects.md` | `tests/e2e/scripts/sections/03_redirects.sh` | Redirects |
| 04 | `sections/04_expiration.md` | `tests/e2e/scripts/sections/04_expiration.sh` | Expiration |
| 05 | `sections/05_fingerprinting.md` | `tests/e2e/scripts/sections/05_fingerprinting.sh` | Privacy/fingerprinting |
| 06 | `sections/06_authentication.md` | `tests/e2e/scripts/sections/06_authentication.sh` | Authentication |
| 07 | `sections/07_permissions.md` | `tests/e2e/scripts/sections/07_permissions.sh` | Authorization |
| 08 | `sections/08_admin_api.md` | `tests/e2e/scripts/sections/08_admin_api.sh` | Admin API |
| 09 | `sections/09_error_handling.md` | `tests/e2e/scripts/sections/09_error_handling.sh` | Error handling |
| 10 | `sections/10_concurrency.md` | `tests/e2e/scripts/sections/10_concurrency.sh` | Concurrency |
| 11 | `sections/11_cli_link_create.md` | `tests/e2e/scripts/sections/11_cli_link_create.sh` | CLI create |
| 12 | `sections/12_cli_link_get.md` | `tests/e2e/scripts/sections/12_cli_link_get.sh` | CLI persisted read |
| 13 | `sections/13_cli_no_server_socket.md` | `tests/e2e/scripts/sections/13_cli_no_server_socket.sh` | CLI/server separation |

## Standard lifecycle

Service sections follow this lifecycle:

1. Reset runtime state and database.
2. Start the section service or mock service.
3. Assert readiness with port/process/HTTP checks.
4. Execute scenario actions.
5. Assert persisted database state with expected snapshots when applicable.
6. Assert runtime state again.
7. Stop processes and clean scratch state.

CLI sections follow this lifecycle:

1. Resolve the built `url_shortener` binary from `URLSHORTENER_BIN` or a known
   build location.
2. Create an isolated temporary working directory when persistence is part of
   the scenario.
3. Execute a one-shot command.
4. Assert exit code and stdout JSON.
5. Execute follow-up commands when the scenario validates persisted state.
6. Assert no server socket remains open for command-mode separation scenarios.
7. Remove temporary state.

## Execution

Run one section:

```bash
bash tests/e2e/scripts/run_section.sh 03_redirects
bash tests/e2e/scripts/run_section.sh 11_cli_link_create
```

Run selected sections:

```bash
QA_SECTIONS="01_server_boot,03_redirects,11_cli_link_create" \
  bash tests/e2e/scripts/run_all_sections.sh
```

Run all CTest-registered e2e tests:

```bash
ctest --test-dir build -L e2e --output-on-failure
```

On Windows, native e2e execution is not expected. Use the Ubuntu CI runner or a
POSIX-compatible environment with Bash and Python 3.

## Assertion rules

- Assert exact status codes, response fields, JSON keys, and persisted state.
- Prefer deterministic expected snapshots for database state.
- Avoid sleeps except short bounded waits around process/socket teardown.
- Do not require external services or network access outside loopback and the
  section-owned mock services.
- Do not log or assert on secrets, DSNs, tokens, private keys, or analytics
  salts.
- Any manual scenario added to this document must include a corresponding
  executable script in `tests/e2e/scripts/sections/`.

## Failure handling

Failed sections are terminal. The runner writes summaries and section artifacts
under `tests/e2e/failures/`. Inspect the section summary first, then stdout,
stderr, logs, and database snapshots.
