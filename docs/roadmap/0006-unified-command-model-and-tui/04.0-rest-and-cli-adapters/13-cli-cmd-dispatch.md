# 13 - `cmd` dispatch: execute, print, exit code

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 12 (parsing), 11 (coercion), 03.0/18 (`build_command_runtime`), 01.0/04 (`serialize_response`, `exit_code_for`), 01.0/07 (`execute`), 01.0/11 (`render_command_help`) (soft, not an ordering edge: job waiting arrives with Task 05.0 subtask 20 as a follow-up edit to this file)
**Blocks:** 15 (shares the runtime/printing helpers; 06.0/19 and 05.0/17 reuse `build_command_runtime`, not this file)
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Implement what `url_shortener cmd <name> ...` does after parsing: build the command runtime (no listener), assemble the request envelope from `--json` / stdin / `--arg`,
execute it as the local operator, print the response envelope (or only `result` with `--raw`) on stdout, and return the process exit code from the shared error table. This is the
CLI adapter of ADR 0002: frame in -> `execute` -> frame out, with no per-command code.

## Dependencies

- Subtasks 11-12; `CmdCliCommand` fields; `ServerConfig::max_request_body_bytes` (verified, 65536) bounds stdin and `--json` length.
- Runtime: `url_shortener::composition::build_command_runtime(const ServerConfig&)` (03.0/18) - per-process here. Caller: `CallerContext{principal = "local-operator", control_set = read_only ? ControlSet::User : ControlSet::Admin, transport = "cli"}`
  (ADR 0002: local operator is Admin, `--read-only` downgrades).
- Output contract (catalog 2 and 7 and the 07.0 CLI shim): stdout = the response envelope on one line followed by `\n`; `--raw` = only `result` JSON for successes; exit code = `exit_code_for(response)` (0 ok, table otherwise: 1 `not_found`, 2 bad input and `unknown_command`/`invalid_envelope`/`unsupported`, 3 `slug_conflict`, 4 storage/internal/`busy`/`job_cancelled`, 5 `not_authorized`).
  Failure with `--raw`: stdout stays empty, stderr gets `cmd failed: <code>: <message>`.
- Existing sugar behavior (unchanged, C4): `link <verb>` prints only the result and writes failures to stderr. `cmd` is additive.
- Jobs (Task 05.0, soft): a `job` response means the command was accepted for background work. This subtask prints the accepted envelope
  (exit 0) and one stderr note "job accepted; waiting is not available in this build" whenever `CommandRuntime` has no `job_runner`.
  Per catalog 4 the final behaviour - wait with `runtime.job_runner->wait_for_terminal`, print the `job.get` envelope, exit 0/4,
  `--no-wait`, and `composition::shutdown_process_job_runtime` before returning - is a **follow-up edit owned by Task 05.0 subtask 20**
  (whichever of the two lands second applies it). No hard dependency on 05.0 (the 04.0 README calls it soft).
- Logging (decision log D14): the one-shot modes must keep stderr exactly as today for successful commands (C4). `main` initializes
  the logger only in server mode, and an uninitialized logger is enabled with a `stderr` sink (verified, `logger_factory.cpp`), so the
  `LoggerAuditSink` that `build_command_runtime` installs (03.0/18) would otherwise print a `command_audit` line on stderr for every
  successful mutation. `DispatchCmdCommand` therefore calls `LoggerFactory::Initialize(one_shot_logging_config(cmd))` before building the
  runtime: **disabled** by default; `--log-file <path>` -> `File` sink (audit lines go there); `--verbose` -> `Stderr` sink. The flags and
  the helper `one_shot_logging_config` are parsed/defined by subtask 12 and shared with the `link <verb>` path (subtask 15).
- C5: the new translation unit must not include `url_shortener/http/` handler headers or Boost.Asio/Beast, must not construct `HttpServer`, and must not touch `UriMapSingleton`/`uri.txt` (grep-checked, same rule as `link_command_dispatch.cpp`).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/cli/cmd_dispatch.hpp` | create | `DispatchCmdCommand`. |
| `src/cli/cmd_dispatch.cpp` | create | Implementation. Add to `COMMON_CPP_SOURCES`. |
| `src/main.cpp` | modify | After the `link` branch: `if (parsed.cmd.has_value()) return url_shortener::cli::DispatchCmdCommand(*parsed.cmd, parsed.config);`. |
| `tests/unit/cli/08_cmd_dispatch.cpp` | create | In-process tests with injected streams. |
| `tests/integration/cli/cli_integration_common.py` | modify (additive) | `run_cli(..., input: str | None = None)` passes `input=` to `subprocess.run`. |
| `tests/integration/cli/20_cmd_link_envelope.py` | create | Process-level envelope and exit-code tests. |
| `tests/integration/cli/21_cmd_inputs_and_read_only.py` | create | `--json -`, `--arg`, `--read-only`, `--dry-run`, `--raw`, help. |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += .cpp; `CLI_UNIT_SOURCES` += 08; `CLI_INTEGRATION_SOURCES` += 20, 21 (Task 02.0 owns 17-19). |

## Classes and functions

```cpp
namespace url_shortener::cli {
/// Executes one `cmd` invocation. Streams are injectable for tests.
int DispatchCmdCommand(const CmdCliCommand& command,
                       const ServerConfig& config,
                       std::istream& in = std::cin,
                       std::ostream& out = std::cout,
                       std::ostream& err = std::cerr);
}
```

Steps inside:

0. `LoggerFactory::Initialize(one_shot_logging_config(command.logging))` (D14: disabled unless `--log-file`/`--verbose`); an initialization error -> `cmd failed: invalid_request: <reason>` on `err`, exit 2.
1. `runtime = composition::build_command_runtime(config)` (a throw from wiring is reported as `cmd failed: internal` on `err`, exit 4).
2. `command.help` -> `out << commands::render_command_help(*descriptor)`; unknown command -> exit 2 with the error envelope (`unknown_command`).
3. Args: `json_from_stdin` -> read at most `config.max_request_body_bytes + 1` bytes from `in` (more -> exit 2, `cmd failed: invalid_envelope: args too large`); else `json_args`; else `coerce_args(descriptor ? descriptor->args : {}, pairs)` (error -> exit 2); else `"{}"`.
4. Build the request envelope text `{"v":1,"id":<id>,"command":<name>,"args":<args_json>,"options":{...}}` with `args_builder`-style escaping for the id/name, after checking that `args_json` is exactly one JSON object with `scan_object` (otherwise exit 2, `cmd failed: invalid_envelope: args must be a JSON object`; this also prevents embedding extra members). Then `response = runtime.registry->execute_json(caller, envelope_text, limits)` so the CLI goes through the same parse path (limits, id rules, closed envelope) as REST and stdio. `id = command.request_id.value_or("cli-1")`.
5. Job handling as described in Dependencies.
6. Print: default `out << serialize_response(response) << '\n'`; `--raw`: success -> `out << result_json << '\n'`, failure -> `err << "cmd failed: " << code << ": " << message << '\n'`.
7. Return `exit_code_for(response)`.

## Implementation steps

1. Implement `DispatchCmdCommand` and wire `main.cpp`.
2. Unit tests with `std::istringstream`/`std::ostringstream`.
3. Add the additive `input` kwarg to the Python helper; write the two integration tests.
4. Register; build the binary and the unit test; run unit, then integration.

## Commands

```powershell
cmake --build cmake-build --target cli__08_cmd_dispatch url_shortener
ctest --test-dir cmake-build -R "^cli__08_cmd_dispatch$" --output-on-failure
ctest --test-dir cmake-build -R "^cli_(20|21)_" --output-on-failure
```

(The integration tests run the built binary via `SIMPLE_HTTP_BIN`, set by CTest; run them directly with `python tests/integration/cli/20_cmd_link_envelope.py` after exporting `SIMPLE_HTTP_BIN`. They work on Windows; the bash `e2e_` sections do not.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/08_cmd_dispatch.cpp` | `cli__08_cmd_dispatch` | unit | `link.create` with `--json` -> exit 0, envelope has `"ok":true` and `result.slug`; same via `--arg target_url=... --arg slug=...` (identical `result` apart from generated id/timestamps); `--raw` prints only the result (valid object, no `"ok"`); `--id c-7` echoed; failures: invalid URL -> exit 2 + `"ok":false`,`invalid_url`; `link.get` unknown -> exit 1 `not_found`; unknown command -> 2 `unknown_command`; `--json 'not json'`, `--json '[1]'` and `--json '{"a":1},"command":"x"'` -> exit 2 with the `invalid_envelope` message (nothing executed); wrong-typed arg -> 2 `invalid_field`; reserved slug -> 2 `reserved_slug`; `--read-only` + `link.create` -> exit 5 `not_authorized`, `--read-only` + `link.list` -> 0; `--dry-run` on `link.create` -> 0 and the link is not stored (`link.get` in the same process -> not_found); `--dry-run` on `link.get` -> 2 `unsupported`; `--json -` reads the stream; stream larger than the limit -> exit 2; `--raw` + failure -> empty stdout, stderr line; `command.list` and `system.info` succeed; `<name> --help` prints the descriptor help and exits 0; no output ever contains the `--json` text on errors; function never throws. |
| `tests/integration/cli/20_cmd_link_envelope.py` | `cli_20_cmd_link_envelope` | integration | spawn the real binary: `cmd link.create --json '{...}'` -> exit 0, one-line JSON envelope `ok:true`; `--raw`; `cmd link.get` unknown -> exit 1; invalid URL -> exit 2 and envelope on stdout; unknown command -> exit 2; process exits promptly (no listener, same technique as `04_cli_exits_no_listening_port.py`). |
| `tests/integration/cli/21_cmd_inputs_and_read_only.py` | `cli_21_cmd_inputs_and_read_only` | integration | `--json -` with `input=`; `--arg` equivalent; `--read-only link.create` -> exit 5; `system.info --read-only` shows `"read_only":true`; `command.list` contains the nine link command names; `db.tables` lists `links` and `click_events` (plus `jobs` once 05.0/21 is wired); **stderr empty** for a successful `cmd link.create` (no audit line, C4/D14); `--log-file <tmp>` -> the file contains exactly one `command_audit` line for that create and stderr stays empty; `--verbose` -> the audit line is on stderr; `cmd --help` exit 0. (The job-waiting cases are added by the 05.0/20 follow-up edit.) |

Manual check: `url_shortener cmd link.create --arg target_url=https://example.com/a --arg slug=a` and compare `result` with `url_shortener link create --url https://example.com/a --slug a` (same fields).

## Constraints

- C1: no per-command code; C2: stdout is the envelope; C4: `link` behavior untouched; C5: no listener, no `uri.txt`, no `UriMapSingleton`; C7: the caller context is the only authorization input.
- Untrusted input: `--json` and stdin are bounded by `max_request_body_bytes`; argv is parsed as data only; error output never echoes the args text.
- Exit codes come only from `exit_code_for` / `error_info_for` (no second table in `src/cli`; `ExitCodeForAppError` remains until subtask 15 maps it onto the table).
- C++17; header does not include Beast/Asio; new `.cpp` in `COMMON_CPP_SOURCES`.

## Success criteria

1. `cli__08_cmd_dispatch`, `cli_20_cmd_link_envelope`, `cli_21_cmd_inputs_and_read_only` pass; existing `cli_01..16` integration tests pass unchanged.
2. `grep -n "http/\|boost/asio\|UriMapSingleton\|HttpServer" src/cli/cmd_dispatch.cpp include/url_shortener/cli/cmd_dispatch.hpp` is empty.
3. For `link.create`, the `result` of `cmd` equals the body of `POST /api/v1/links` for the same input modulo generated id/timestamps (cross-checked by Task 07.0's conformance corpus; spot-check by hand now).

## Out of scope / risks

- Persistence across invocations requires `--storage-config` (Task 02.0); in default mode every `cmd` process starts with an empty in-memory store, as `link` does today.
- The exit code of a waited job (0/4) and the waiting itself are delivered by the 05.0/20 follow-up edit; until then `cmd` prints the accepted envelope.
- Parse errors (subtask 12) still exit 1 through `main()`'s catch; only execution errors use the table.
