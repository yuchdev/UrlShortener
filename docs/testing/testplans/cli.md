# CLI Test Plan

## Scope

CLI tests verify one-shot command mode over the shared application command
layer. CLI mode must execute a command, print a result, persist expected state,
and exit without starting `HttpServer`.

## Commands under test

- `link create --url <url> [--slug <slug>] [--base-domain <url>]`
- `link get --slug <slug>`
- `link get --id <id>`
- `link stats --slug <slug> --from <epoch> --to <epoch> --bucket hour|day|week`

## Required coverage

| Layer | Coverage |
|---|---|
| Unit | Parser maps argv to command DTOs; app service validates URL, slug, reserved slug, duplicate slug, stats windows, and storage errors. |
| Integration | Spawn the binary, assert stdout JSON, compare create/get data, verify state in a temp working directory, and reject invalid/private target URLs. |
| E2E | `11_cli_link_create`, `12_cli_link_get`, and `13_cli_no_server_socket`. |

## Acceptance criteria

- Success writes a single JSON object to stdout.
- Invalid input returns non-zero and does not create active link state.
- Follow-up CLI commands can read created state when run in the same working
  directory.
- Command mode does not bind port `8000` or run the server event loop.

## E2E analogs

- `tests/e2e/scripts/sections/11_cli_link_create.sh`
- `tests/e2e/scripts/sections/12_cli_link_get.sh`
- `tests/e2e/scripts/sections/13_cli_no_server_socket.sh`
