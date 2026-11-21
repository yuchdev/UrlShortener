# CLI Test Plan

## Scope

CLI tests verify one-shot command mode and the stdio/cmd entry points over the
shared application command layer. CLI mode must execute a command, print a
result, and exit without starting `HttpServer`.

The storage backend is selected at startup from `--storage-config`. The default
is in-memory (per-process, state not shared across separate invocations). With a
SQLite backend, state is durable and shared between invocations that use the same
database file.

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
- `link stats --slug <slug> --from <epoch> --to <epoch> --bucket hour|day|week`

Additional modes over the shared command registry:

- `cmd <command> [--arg key=value ...] [--id <id>] [--read-only]` — sends a
  single command envelope to the registry and exits.
- `serve-stdio` — NDJSON line-per-request session over stdin/stdout; exits on
  EOF with no HTTP listener.
- `tui` — interactive terminal UI; a non-TTY stdin causes immediate exit with
  code `2` and no listener.

## Required coverage

| Layer | Files | Coverage |
|-------|-------|----------|
| Unit – command service | `tests/unit/app/13_link_command_service_update_field_semantics.cpp`, `14_link_command_service_delete_soft_delete.cpp`, `15_link_command_service_set_enabled_toggle.cpp`, `16_link_command_service_restore_clears_deleted_at.cpp`, `17_link_command_service_preview_link.cpp` | Service validates URL, slug, reserved slug, duplicate slug, stats windows, storage errors; all nine service methods branch-covered. |
| Unit – CLI parser | `tests/unit/cli/01_cli_parser_link_commands.cpp` | All nine verbs parse into the correct DTOs; all server-mode flags unchanged; invalid/empty flag values rejected at parse time. |
| Unit – dispatch & lifecycle | `tests/unit/cli/02_cli_dispatch_reachable.cpp` | `DispatchLinkCommand` reachable; in-process create-then-get round-trip observable. |
| Unit – output & exit codes | `tests/unit/cli/03_cli_success_output.cpp`, `tests/unit/cli/04_cli_exit_codes.cpp` | Success writes one JSON line to stdout; failure writes empty stdout, diagnostic to stderr; exit codes 0-4 map to `AppErrorCode` exhaustively. |
| Unit – `cmd` and `serve-stdio` | `tests/unit/cli/07_cli_parser_cmd.cpp`, `08_cmd_dispatch.cpp`, `09_link_args_json.cpp`, `10_link_dispatch_via_registry.cpp`; `tests/unit/commands/61_ndjson_line_reader.cpp`, `62_ndjson_session.cpp`, `63_stdio_adapter.cpp` | `cmd` argv parses into an envelope; `--arg` coercion; `serve-stdio` session lifecycle, EOF, oversized lines. |
| Unit – `tui` | `tests/unit/cli/30_cli_parser_tui_mode.cpp`, `31_cli_parser_tui_path_flags.cpp` | `tui` mode flags parse; `--path-root`/`--no-file-paths` wired. |
| Integration | `tests/integration/cli/01-18_*.py` (link commands), `20_cmd_link_envelope.py`, `21_cmd_inputs_and_read_only.py`, `40_serve_stdio_session.py`, `41_serve_stdio_jobs_and_eof.py` | Spawn the binary; assert stdout JSON shape; assert exit codes; reject invalid/private target URLs. Cross-invocation state persistence via SQLite backend (`cli_18`). `cmd` envelope and read-only mode. `serve-stdio` sessions with job dispatch. |
| Conformance | Corpus suites `10`..`42` via shims `cli_cmd` and `stdio` | `cmd` and `serve-stdio` shims must produce the same envelope result as every other transport for every corpus case. |
| E2E | `tests/e2e/scripts/sections/11-19_*.sh`, `30_serve_stdio.sh` | No-socket guarantee for all nine verbs and for `cmd`/`serve-stdio`/`tui`; create/get output shape; no server started; full NDJSON session. |

## Acceptance criteria

- Success writes exactly one JSON object to stdout and exits `0`.
- Invalid input (bad URL, reserved slug, missing required flag) returns a
  non-zero exit code and leaves stdout empty.
- CLI mode does not bind port `8000` or run the server event loop for any of
  the nine verbs, `cmd`, `serve-stdio`, or `tui`.
- With the in-memory backend (default), two separate CLI process invocations do
  not share link state. With a SQLite backend, state is shared between
  invocations that use the same database file.

## E2E analogs

- `tests/e2e/scripts/sections/11_cli_link_create.sh`
- `tests/e2e/scripts/sections/12_cli_link_get.sh`
- `tests/e2e/scripts/sections/13_cli_no_server_socket.sh`
- `tests/e2e/scripts/sections/14_cli_link_update.sh`
- `tests/e2e/scripts/sections/15_cli_link_delete.sh`
- `tests/e2e/scripts/sections/16_cli_link_enable_disable.sh`
- `tests/e2e/scripts/sections/17_cli_link_restore.sh`
- `tests/e2e/scripts/sections/18_cli_link_preview.sh`
- `tests/e2e/scripts/sections/19_cmd_serve_stdio_no_listener.sh`
- `tests/e2e/scripts/sections/30_serve_stdio.sh`
