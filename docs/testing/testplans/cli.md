# CLI Test Plan

## Scope

CLI tests verify one-shot command mode over the shared application command
layer. CLI mode must execute a command, print a result, and exit without
starting `HttpServer`. Each CLI invocation begins from an empty in-memory
link store (see Storage note below).

## Storage note

`linkRepository()` is a function-local `static` in-memory singleton. Two
separate CLI process invocations do **not** share state: a `link create` in
one invocation is invisible to a `link get` in a second, separately-launched
process. Integration and e2e tests are scoped accordingly (error paths and
single-invocation success paths only; cross-process create-then-read is not
testable with the current in-memory backend).

## Commands under test

`link get` covers two REST endpoint rows (`GET /api/v1/links/{slug}` and
`GET /api/v1/links/id/{id}`) via its `--slug`/`--id` selector flags; the
other eight verbs map one-to-one to a REST operation, giving nine distinct
CLI verbs in total:

- `link create --url <url> [--slug <slug>] [--base-domain <url>]`
- `link get --slug <slug> | --id <id>`
- `link update --slug <slug> [--enabled <bool>] [--expires-at
  <rfc3339>|clear] [--tags <csv>] [--metadata <k=v,...>] [--campaign-id
  <id>] [--clear-campaign]`
- `link delete --slug <slug>`
- `link enable --slug <slug>`
- `link disable --slug <slug>`
- `link restore --slug <slug>`
- `link preview --slug <slug> | --id <id>`
- `link stats --slug <slug> --from <epoch> --to <epoch> --bucket
  hour|day|week`

## Required coverage

| Layer | Files | Coverage |
|-------|-------|----------|
| Unit – command service | `tests/unit/app/13_link_command_service_update_field_semantics.cpp`, `14_link_command_service_delete_soft_delete.cpp`, `15_link_command_service_set_enabled_toggle.cpp`, `16_link_command_service_restore_clears_deleted_at.cpp`, `17_link_command_service_preview_link.cpp` | Service validates URL, slug, reserved slug, duplicate slug, stats windows, storage errors; all nine service methods branch-covered. |
| Unit – CLI parser | `tests/unit/cli/01_cli_parser_link_commands.cpp` | All nine verbs parse into the correct DTOs; all server-mode flags unchanged; invalid/empty flag values rejected at parse time. |
| Unit – dispatch & lifecycle | `tests/unit/cli/02_cli_dispatch_reachable.cpp` | `DispatchLinkCommand` reachable; in-process create-then-get round-trip observable. |
| Unit – output & exit codes | `tests/unit/cli/03_cli_success_output.cpp`, `tests/unit/cli/04_cli_exit_codes.cpp` | Success writes one JSON line to stdout; failure writes empty stdout, diagnostic to stderr; exit codes 0-4 map to `AppErrorCode` exhaustively. |
| Integration | `tests/integration/cli/01-10_*.py` (pre-existing create/get/stats contracts), `11_link_update_not_found_and_invalid_input.py`, `12_link_delete_not_found_and_invalid_input.py`, `13_link_enable_not_found_and_invalid_input.py`, `14_link_disable_not_found_and_invalid_input.py`, `15_link_restore_not_found_and_invalid_input.py`, `16_link_preview_not_found_and_invalid_input.py` | Spawn the binary; assert stdout JSON shape; assert exit codes; reject invalid/private target URLs. Error-path (not_found, missing/invalid flags) coverage for commands whose happy path requires cross-process seeding. |
| E2E | `tests/e2e/scripts/sections/11-18_cli_*.sh` | No-socket guarantee for all nine verbs; create/get output shape; no server started. |

## Acceptance criteria

- Success writes exactly one JSON object to stdout and exits `0`.
- Invalid input (bad URL, reserved slug, missing required flag) returns a
  non-zero exit code and leaves stdout empty.
- CLI mode does not bind port `8000` or run the server event loop for any of
  the nine verbs.
- Two separate CLI process invocations do not share link state (in-memory
  backend is per-process; cross-process create-then-read is not a supported
  use case with the current backend).

## E2E analogs

- `tests/e2e/scripts/sections/11_cli_link_create.sh`
- `tests/e2e/scripts/sections/12_cli_link_get.sh`
- `tests/e2e/scripts/sections/13_cli_no_server_socket.sh`
- `tests/e2e/scripts/sections/14_cli_link_update.sh`
- `tests/e2e/scripts/sections/15_cli_link_delete.sh`
- `tests/e2e/scripts/sections/16_cli_link_enable_disable.sh`
- `tests/e2e/scripts/sections/17_cli_link_restore.sh`
- `tests/e2e/scripts/sections/18_cli_link_preview.sh`
