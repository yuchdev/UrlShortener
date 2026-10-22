# 15 - `link <verb>` dispatch through the registry

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 14 (converters), 13 (`cmd` dispatch: runtime/caller conventions; `one_shot_logging_config` comes from 12 through 13), 03.0/18 (`build_command_runtime`), 01.0/02 (`error_info_for`, `from_app_error`), 01.0/04 (`exit_code_for`)
**Blocks:** none (closes the task; Task 07.0 sugar-equivalence tests build on it)
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Re-point the nine `link <verb>` commands from direct `LinkCommandService` calls to the registry, so CLI sugar, `cmd`, REST and (later) the TUI all execute the same descriptor/handler,
while stdout, stderr text and exit codes stay exactly what the CLI documents today (contract C4). Also remove the second exit-code table by making `ExitCodeForAppError` read the shared one.

## Dependencies

- Verified current dispatch (`src/cli/link_command_dispatch.cpp`): `DispatchLinkCommand` builds `app::BuildLegacyLinkCommandService(config)`, switches on `LinkCliVerb`, calls the matching service method and prints
  - success: exactly one line on stdout = the REST serializer's JSON (`serializeLinkViewJson` / `serializeLinkPreviewJson` for `preview` / `serializeLinkStatsJson`) + `\n`;
  - failure: stdout empty, stderr `"<verb> failed: <error.detail>\n"`, exit code `ExitCodeForAppError(error.code)` (0 none, 1 not_found, 2 invalid_url/invalid_slug/invalid_field/reserved_slug, 3 slug_conflict, 4 storage_failure/internal).
  Tests: `tests/unit/cli/02_cli_dispatch_reachable.cpp`, `03_cli_success_output.cpp`, `04_cli_exit_codes.cpp` (includes a pure `ExitCodeForAppError` mapping test), integration `tests/integration/cli/01..16`, bash `e2e_11..18`.
- Shared table: `error_info_for(from_app_error(code)).exit_code` equals `ExitCodeForAppError(code)` for every code (asserted by 01.0/02's parity test).
- Service error `detail` strings become `CommandError::message` via `to_command_error` (03.0/05), so stderr text for service-originated failures is unchanged. Errors that the parser used to catch (missing `--slug`, bad flag) never reach dispatch.
- Registry-originated errors that did not exist before (a value rejected by an `ArgSpec` cap) cannot occur for CLI input because the caps for the nine commands are 65536/4096 (03.0/07) and argv is far smaller.
- Config flags `--base-domain` / `--allow-private-targets` are already applied to the `ServerConfig` by `parseCreateArgs`; `build_command_runtime(config)` receives that config.
- `--read-only` for `link` verbs is **not** added (C4: no new flags on existing verbs); `cmd --read-only` is the way to run a command as `ControlSet::User`.
- Logging (decision log D14): the runtime installs a `LoggerAuditSink` (03.0/18), and an uninitialized logger is enabled with a
  `stderr` sink (verified, `logger_factory.cpp`; `main` initializes logging only in server mode). Without action every successful
  mutating verb would gain a `command_audit` line on stderr, breaking C4. `DispatchLinkCommand` therefore calls
  `LoggerFactory::Initialize(one_shot_logging_config(OneShotLogging{}))` (subtask 12; i.e. **disabled**) before building the runtime.
  `link` verbs get no `--log-file`/`--verbose` flags (C4); an operator who needs the audit trail uses `cmd ... --log-file`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `src/cli/link_command_dispatch.cpp` | modify | Replace the service switch with: converter -> `execute_json`/`execute` -> print. `report_result`, `serialize_success`, `report_preview_result` removed. `ExitCodeForAppError` becomes a one-line read of the shared table. |
| `include/url_shortener/cli/link_command_dispatch.hpp` | modify | Documentation only (still declares `ExitCodeForAppError` and `DispatchLinkCommand`; remove the mapping list from the Doxygen comment and point to the table). |
| `tests/unit/cli/10_link_dispatch_via_registry.cpp` | create | Parity tests (written first against the old dispatch where possible). |
| `sources.cmake` | modify | `CLI_UNIT_SOURCES` += the test (target `cli__10_link_dispatch_via_registry`). |

## Classes and functions

No new public symbols. Dispatch outline:

```cpp
int DispatchLinkCommand(const LinkCliCommand& command, const ServerConfig& config)
{
    LoggerFactory::Initialize(one_shot_logging_config(OneShotLogging{}));   // D14: disabled, stderr stays as today
    const auto runtime = composition::build_command_runtime(config);
    const std::string args_json = <per-verb converter from subtask 14>(std::get<...>(command.payload));   // enable/disable: slug_args_json
    CommandRequest request{1, "cli-link", std::string(command_name_for(command.verb)), args_json, {}};
    const CallerContext caller{"local-operator", ControlSet::Admin, "cli"};
    const auto response = runtime.registry->execute(caller, request);
    if (response.ok) { std::cout << response.result_json << '\n'; return 0; }
    std::cerr << verb_token(command.verb) << " failed: " << response.error.message << '\n';
    return error_info_for(response.error.code).exit_code;
}
int ExitCodeForAppError(app::AppErrorCode code) { return commands::error_info_for(commands::from_app_error(code)).exit_code; }
```

(`verb_token` stays: it is the human-readable verb used in diagnostics.)

## Implementation steps

1. **Characterize first:** add `10_link_dispatch_via_registry.cpp` whose "old behavior" cases (stdout line, stderr text, exit code per verb and per error class) are asserted against the unmodified dispatch; run green.
2. Switch the dispatch to the registry; keep `verb_token`; delete the dead helpers; make `ExitCodeForAppError` table-driven.
3. Add the `cmd` vs `link` result-parity cases to the same test file.
4. Run all CLI unit tests, then the CLI integration tests, then (non-Windows) the bash `e2e_11..18`.

## Commands

```powershell
cmake --build cmake-build --target cli__10_link_dispatch_via_registry cli__02_cli_dispatch_reachable cli__03_cli_success_output cli__04_cli_exit_codes url_shortener
ctest --test-dir cmake-build -R "^cli__" --output-on-failure
ctest --test-dir cmake-build -R "^cli_[0-9]+_" --output-on-failure      # integration tests that spawn the binary (Python)
ctest --test-dir cmake-build -R "^e2e_1[1-8]_" --output-on-failure      # POSIX only; skipped on Windows hosts
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/10_link_dispatch_via_registry.cpp` | `cli__10_link_dispatch_via_registry` | unit | for each of the nine verbs: success writes exactly one stdout line equal to the corresponding serializer output (preview: reduced body; stats: range report) and nothing on stderr; **`link create` success leaves stderr completely empty** (captured `std::cerr` rdbuf; no `command_audit` line, D14), likewise `update`/`delete`/`enable`/`disable`/`restore`; failures per class: `get` unknown -> exit 1 + `get failed: Link not found` + empty stdout; `create` invalid URL -> 2; reserved slug -> 2; invalid slug -> 2; duplicate slug -> 3; storage failure (forced through the legacy store seam if available, otherwise through a registry with a failing fake injected via the lister/service seam) -> 4; `update`/`delete`/`enable`/`disable`/`restore`/`preview` unknown slug -> 1; `stats` invalid timestamp -> 2 and invalid bucket -> 2 with the legacy detail text; `ExitCodeForAppError` mapping for every `AppErrorCode` unchanged (the existing 04 test passes unmodified); stdout stays empty on every failure; `cmd link.create --arg ...` and `link create --url ...` return the same `result` modulo id/timestamps (compare after masking); `--base-domain` flows into `short_url`; `--allow-private-targets true` allows a loopback target; the process never constructs an `HttpServer` (link-time check: the test binary does not reference it). |

Manual check: run the nine verbs from `docs/cli/README.md` examples before and after on the same checkout; diff stdout/stderr/exit codes.

## Constraints

- C4: stdout/stderr text, one-line JSON, exit codes and flags of `link <verb>` unchanged; pre-existing CLI tests are not edited.
- C1: no `LinkCommandService`, `ILinkStore` or DTO call remains in `link_command_dispatch.cpp`.
- C5: still no `HttpServer`, `io_context`, socket, `uri.txt` or `UriMapSingleton` in this translation unit (grep in success criteria).
- The single exit-code table lives in 01.0/02; this file reads it.
- C++17; naming per `.clang-tidy`.

## Success criteria

1. `ctest -R "^cli__"` and `ctest -R "^cli_[0-9]+_"` pass with the pre-existing tests unmodified; `e2e_11..18` pass on POSIX.
2. `grep -n "LinkCommandService\|BuildLegacyLinkCommandService\|report_result\|serialize_success" src/cli/link_command_dispatch.cpp` is empty.
3. `grep -n "http/\|boost/asio\|UriMapSingleton\|HttpServer" src/cli/link_command_dispatch.cpp` is empty.
4. The only exit-code mapping in `src/cli` is the one-line `ExitCodeForAppError` over the shared table.

## Out of scope / risks

- Persistence across invocations: Task 02.0 (`--storage-config`); in default mode each `link` process still starts with an empty in-memory store (documented limitation, unchanged).
- Per-process overhead: one `build_command_runtime` per invocation (registers ~16 descriptors); negligible for a one-shot tool.
- `docs/cli/README.md` text changes (new `cmd` section, the storage note) are Task 08.0 subtask 08.
