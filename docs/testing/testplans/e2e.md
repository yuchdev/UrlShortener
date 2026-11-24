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
- CLI create/get/stat/update/delete/enable/disable/restore/preview command flows.
- CLI/server separation: command mode must not open the default HTTP port.
- `cmd`, `serve-stdio`, and `tui` no-listener guarantee (section 19).
- Full `serve-stdio` NDJSON session with job dispatch and clean EOF (section 30).

## Acceptance criteria

- Every manual section spec maps to an executable section script.
- Sections are isolated and do not depend on shared state from previous runs.
- Runtime state and persisted state are asserted explicitly.
- Linux/POSIX-only requirements are documented.
- All time bounds use `run_with_timeout` from `tests/e2e/scripts/common.sh`
  (prefers `timeout`, then `gtimeout`, then a pure-shell watchdog). Do not add
  hardcoded `sleep` calls where a port probe or process check is sufficient.

## Sections

| Section script | Manual scenario | CTest target |
|---|---|---|
| `01_server_boot.sh` | Server lifecycle | `e2e_01_server_boot` |
| `02_create_short_url.sh` | REST link creation | `e2e_02_create_short_url` |
| `03_redirects.sh` | Redirect behavior | `e2e_03_redirects` |
| `04_expiration.sh` | Expiration | `e2e_04_expiration` |
| `05_fingerprinting.sh` | Fingerprinting | `e2e_05_fingerprinting` |
| `06_authentication.sh` | Authentication | `e2e_06_authentication` |
| `07_permissions.sh` | Permissions | `e2e_07_permissions` |
| `08_admin_api.sh` | Admin API | `e2e_08_admin_api` |
| `09_error_handling.sh` | Error handling | `e2e_09_error_handling` |
| `10_concurrency.sh` | Concurrency | `e2e_10_concurrency` |
| `11_cli_link_create.sh` | CLI link create | `e2e_11_cli_link_create` |
| `12_cli_link_get.sh` | CLI link get | `e2e_12_cli_link_get` |
| `13_cli_no_server_socket.sh` | CLI/server separation | `e2e_13_cli_no_server_socket` |
| `14_cli_link_update.sh` | CLI link update | `e2e_14_cli_link_update` |
| `15_cli_link_delete.sh` | CLI link delete | `e2e_15_cli_link_delete` |
| `16_cli_link_enable_disable.sh` | CLI link enable/disable | `e2e_16_cli_link_enable_disable` |
| `17_cli_link_restore.sh` | CLI link restore | `e2e_17_cli_link_restore` |
| `18_cli_link_preview.sh` | CLI link preview | `e2e_18_cli_link_preview` |
| `19_cmd_serve_stdio_no_listener.sh` | `cmd`/`serve-stdio`/`tui` no-listener | `e2e_19_cmd_serve_stdio_no_listener` |
| `30_serve_stdio.sh` | `serve-stdio` NDJSON session | `e2e_30_serve_stdio` |

Sections `14`-`18` extend the CLI/server separation guarantee from section `13`
to the remaining link verbs. Section `19` extends the guarantee to `cmd`,
`serve-stdio`, and non-TTY `tui`, and also verifies the `uri.txt` sentinel is
never written. See
[`docs/testing/e2e_testing/sections/19_cmd_serve_stdio_no_listener.md`](../e2e_testing/sections/19_cmd_serve_stdio_no_listener.md)
for the detailed scenario spec.

## Commands

```bash
bash tests/e2e/scripts/run_section.sh 11_cli_link_create
bash tests/e2e/scripts/run_section.sh 19_cmd_serve_stdio_no_listener
ctest --test-dir build -L e2e --output-on-failure
```
