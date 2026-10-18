# 21 - Security: no listener, no URI store

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Tasks 04.0 (`cmd`), 05.0 (`serve-stdio`), 06.0 (`tui`)
**Blocks:** 34
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Enforce plan.md C5 for the three new entry points: `cmd`, `serve-stdio` and
`tui` never construct `HttpServer`, never bind or listen on any socket, and
never create or read `uri.txt` / `UriMapSingleton`. Extends the existing
`link` checks (`tests/integration/cli/04_cli_exits_no_listening_port.py`,
`tests/e2e/scripts/sections/13_cli_no_server_socket.sh`).

## Dependencies

- Existing pattern: port-probe in `13_cli_no_server_socket.sh` (python socket connect to 127.0.0.1:8000) and `04_cli_exits_no_listening_port.py`.
- `tui` requires a PTY; the test uses `tui --help`-style non-interactive exit if 06.0 provides one (e.g. `tui --version-check` or EOF on stdin makes it exit) - **UNVERIFIED**; otherwise only the structural check (below) covers `tui`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/py/32_no_listener_process_level.py` | create | cross-platform process checks |
| `tests/e2e/scripts/sections/19_cmd_serve_stdio_no_listener.sh` | create | POSIX socket-table check (`/proc/net/tcp*` or `ss`) |
| `tests/unit/commands/79_no_http_server_in_local_modes_test.cpp` | create | structural source-scan test |
| `sources.cmake` | modify | Append python test to `CONFORMANCE_PY_SOURCES`, unit test to `COMMANDS_UNIT_SOURCES`; add `19_cmd_serve_stdio_no_listener` to `E2E_CLI_SECTIONS` |

`CMakeLists.txt` also needs `target_compile_definitions(commands__79_no_http_server_in_local_modes_test PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")` (the `COMMANDS_UNIT_SOURCES` registration of Task 01.0 subtask 01 is generic). E2E section numbers `11`-`18` exist and Task 05.0 subtask 18 uses `30`; `19` is reserved for this subtask. Task 07.0 owns `tests/unit/commands/70-89` (plan.md "Test file prefixes").

## Classes and functions

`32_no_listener_process_level.py` tests:

| Test | Assertion |
|---|---|
| `test_cmd_exits_and_no_port` | `cmd command.list` exits < 5 s; ports 8000, 8080, 8443 and the default HTTP/HTTPS ports from `ServerConfig` closed afterwards |
| `test_cmd_during_run_no_listener` | start `cmd link.import` of a slow payload (or `serve-stdio`), enumerate listening sockets of that PID via `psutil`-free method: Windows `netstat -ano` parse / POSIX `/proc/<pid>/net/tcp*` + `ss -ltnp`; assert none owned by the PID |
| `test_serve_stdio_no_listener` | start, send `command.list`, assert PID owns no LISTEN socket, close stdin |
| `test_no_uri_txt_created` | run `cmd`, `serve-stdio` in an empty cwd; `uri.txt` does not exist afterwards; pre-create a `uri.txt` with sentinel content and assert it is not modified (mtime and bytes) and its keys are not visible via any command |
| `test_server_flag_with_cmd_rejected_or_ignored` | `cmd --port 9999 command.list` does not listen on 9999 |
| `test_tui_non_interactive_exit_no_listener` | only if 06.0 offers non-interactive exit; else skipped with reason |

`19_cmd_serve_stdio_no_listener.sh`: same checks in shell with `ss -ltnp | grep <pid>` / `/proc/net/tcp`, mirroring section 13's style (`set -euo pipefail`, `URLSHORTENER_BIN`, skip if port 8000 busy).

`79_no_http_server_in_local_modes_test.cpp` (structural, mirrors the redirect guard idea, C5/C8):

| Case | Assertion |
|---|---|
| `cmd_dispatch_has_no_http_server_symbols` | source files of `cmd`/`serve-stdio`/`tui` entry (listed in a table inside the test, resolved via `SOURCE_DIR`) do not include `http/http_server.h` / `http_session.h` nor reference `HttpServer`, `acceptor`, `uri_map_singleton` |
| `tui_sources_include_no_service_headers` | every file under `src/tui/` and `include/url_shortener/tui/` includes none of `app/link_command_service`, `storage/`, `core/link_service`, `security/*service*` headers (C8) - allowlist: `commands/envelope.hpp`, `commands/arg_spec.hpp`, `commands/json_scan.hpp`, `tui/command_client.hpp`, `security/control_set.hpp`; `commands/command_registry.hpp` and `composition/command_runtime.hpp` only in `in_process_client.*` and `tui_entry.cpp` (06.0/02 boundary test is the primary guard; this is the cross-task duplicate check) |
| `local_modes_build_no_registry` | `src/cli/cmd_dispatch.cpp`, `src/cli/serve_stdio_dispatch.cpp`, `src/tui/tui_entry.cpp` call `build_command_runtime` and construct no `CommandRegistry`/`JobRunner` (plan C11) |
| `src_commands_do_not_touch_uri_store` | `src/commands/**`, `src/transport/**` do not mention `UriMapSingleton`/`uri.txt` |

## Implementation steps

1. Process tests with a small `listening_sockets(pid)` helper per OS (Windows: parse `netstat -ano -p tcp`; Linux/macOS: `/proc` or `lsof -nP -iTCP -sTCP:LISTEN -a -p <pid>`); skip with message when the tool is missing.
2. Structural test: simple line-based scanning with `std::ifstream`; fail with `file:line`.
3. Shell section modeled on `13_cli_no_server_socket.sh`.

## Commands

```bash
ctest --test-dir cmake-build -R "^(conformance_32_|commands__79_|e2e_19_)" --output-on-failure
# e2e_19 registered only on non-Windows hosts (CMake skips e2e on CMAKE_HOST_WIN32)
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/py/32_no_listener_process_level.py` | `conformance_32_no_listener_process_level` | `integration;conformance` | 6 |
| `tests/e2e/scripts/sections/19_cmd_serve_stdio_no_listener.sh` | `e2e_19_cmd_serve_stdio_no_listener` | `e2e` | 3 checks |
| `tests/unit/commands/79_no_http_server_in_local_modes_test.cpp` | `commands__79_no_http_server_in_local_modes_test` | `unit` | 4 |

## Constraints

- Tests must not require port 8000 to be free to pass: if it is in use, skip the probe with a message (same convention as section 13) but still run the PID-ownership check.
- No admin privileges needed; no firewall prompts (Windows loopback only; tests never listen).
- Structural scan is a tripwire, not proof; the process checks are the proof.

## Success criteria

1. A deliberately introduced `HttpServer` construction in `cmd` mode fails both the process and structural tests.
2. `uri.txt` sentinel unchanged after all three modes.
3. Skips (missing OS tools) are explicit and counted in subtask 34's report.

## Out of scope / risks

- TUI interactive behavior (needs a PTY): manual checklist (subtask 29).
- Risk: `src/tui/` layout may differ from plan.md file map; update the scan table when 06.0 lands.
