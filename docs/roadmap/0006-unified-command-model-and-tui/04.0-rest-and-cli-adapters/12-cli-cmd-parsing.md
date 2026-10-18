# 12 - `cmd` subcommand parsing

**Parent task:** 04.0 REST and CLI adapters
**State:** ⬜ Not started
**Depends on:** 01.0/03 (`is_valid_command_name`), 02.0/10 (`--storage-config` helper) (coordination only, no dependency: Task 05.0 subtask 16 parses `serve-stdio` in the same files)
**Blocks:** 13
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Teach `CliParser` the new top-level one-shot mode `url_shortener cmd <name> [options]` and carry the result in `ParseResult` without
touching how `link <verb>` and server mode parse. This subtask only turns `argv` into a plain `CmdCliCommand` (strings and booleans); nothing is executed and the
registry is not touched, so it can be reviewed and tested with the parser alone.

## Dependencies

- Verified current structure (`src/cli_parser.cpp`, `include/url_shortener/cli/cli_parser.h`): `CliParser::parse` loads environment defaults, then if `argv[1] == "link"` it returns early
  after `linkHelpRequested` / `parseLinkCommand`, filling `ParseResult::command` (`std::optional<LinkCliCommand>`); otherwise Boost.ProgramOptions parses server flags.
  Parse errors are `std::invalid_argument`, which `main()` catches (logs and exits 1; unchanged and shared with `link`).
- `is_valid_command_name(std::string_view)` (01.0/03) validates the dotted-name shape without a regex.
- Coordination: Task 05.0 subtask 16 adds `serve-stdio` to the same function and may restructure `ParseResult` (a mode enum). Land whichever comes first and adapt the other; this
  spec assumes the minimal additive field `std::optional<CmdCliCommand> cmd`.
- `--storage-config <file>` (Task 02.0 subtask 10) must be accepted by `cmd` with the same meaning it has for `link`. 02.0/10 specifies the field `ServerConfig::storage_config_path` and the file-local helper `extract_storage_config_flag(std::vector<std::string>&, ServerConfig&)` in `src/cli_parser.cpp`; the `cmd` branch calls that helper before parsing its own flags (no second parser).
- Decision: `--read-only` is a flag of `cmd` (and of `link` verbs in subtask 15) that downgrades the local session to `ControlSet::User` (ADR 0002 Authorization). Position: any place after the command name.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/cli/cli_parser.h` | modify | Add `struct CmdCliCommand` and `std::optional<CmdCliCommand> cmd;` to `ParseResult`. |
| `src/cli_parser.cpp` | modify | Add `parseCmdCommand`, `buildCmdHelpText`; add the `argv[1] == "cmd"` branch next to the `link` branch. |
| `tests/unit/cli/07_cli_parser_cmd.cpp` | create | Parser tests. |
| `sources.cmake` | modify | `CLI_UNIT_SOURCES` += the test (target `cli__07_cli_parser_cmd`). |

## Classes and functions

```cpp
// cli_parser.h
struct CmdCliCommand
{
    std::string command;                                   ///< dotted name, shape-checked only
    std::optional<std::string> json_args;                  ///< --json <text> (not "-")
    bool json_from_stdin = false;                          ///< --json -
    std::vector<std::pair<std::string, std::string>> arg_pairs;  ///< --arg <name>=<value>, in order
    std::optional<std::string> request_id;                 ///< --id <correlation id>
    bool raw = false;        ///< --raw: print only `result`
    bool dry_run = false;    ///< --dry-run -> options.dry_run
    bool async = false;      ///< --async   -> options.async
    bool no_wait = false;    ///< --no-wait: do not wait for a job
    bool read_only = false;  ///< --read-only: run as ControlSet::User
    bool help = false;       ///< <name> --help: dispatch prints the descriptor's help
};
// ParseResult gains:  std::optional<CmdCliCommand> cmd;
```

Accepted options (all after `cmd <name>`; order free): `--json <text|->`, `--arg <name>=<value>` (repeatable), `--id <text>`, `--raw`, `--dry-run`, `--async`, `--no-wait`,
`--read-only`, `--help`/`-h`, plus the service-config flags `link create` already accepts: `--base-domain <url>`, `--allow-private-targets <bool>` (applied to `ParseResult::config`,
same parsing helpers as `parseCreateArgs`).

Errors (`std::invalid_argument`, messages never echo `--json` content): missing name; invalid name shape; unknown option; option missing its value; `--json` repeated; `--json` empty;
`--json` together with `--arg`; `--id` empty or longer than `ServerConfig::request_id_max_length`; stray positional after the name. `url_shortener cmd --help` (no name) sets
`help_requested` with the static usage text (options above and one example); `cmd <name> --help` sets `CmdCliCommand::help` instead (rendered by subtask 13, which has the registry).

## Implementation steps

1. Add `CmdCliCommand` and `ParseResult::cmd`.
2. Implement `parseCmdCommand` (token loop over `std::vector<std::string>`), reusing the existing `parseBool` helper for `--allow-private-targets`.
3. Add the top-level branch for `argv[1] == "cmd"` (mirrors the `link` branch; no server-flag parsing).
4. Tests; register; build; run all `cli__` tests to prove `link` and server parsing are unchanged.

## Commands

```powershell
cmake --build cmake-build --target cli__07_cli_parser_cmd cli__01_cli_parser_link_commands
ctest --test-dir cmake-build -R "^cli__0[1-7]_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/07_cli_parser_cmd.cpp` | `cli__07_cli_parser_cmd` | unit | `cmd link.get --json '{"slug":"a"}'` -> fields set, `result.cmd` engaged, `result.command` empty; `--arg` repeated keeps order; `--json -` sets `json_from_stdin`; every boolean flag individually and combined; `--id abc`; `--base-domain` and `--allow-private-targets true` reach `result.config`; options before and after the name; `cmd` alone -> `std::invalid_argument`; bad names (`Link.Get`, `link`, `link..get`, empty, 65 chars) -> error; unknown flag -> error; `--json` missing value, repeated, empty, with `--arg` -> error; `--id` empty / 65 chars -> error; stray positional -> error; error text does not contain the supplied JSON; `cmd --help` -> `help_requested` with text mentioning `--json`, `--arg`, `--raw`, `--no-wait`, `--read-only`; `cmd link.get --help` -> `cmd->help == true`; `link get ...` and server flags (`--http-port 9000`) parse exactly as before (reuse cases from `01_cli_parser_link_commands.cpp`); `cmd` is recognized only as `argv[1]` (`--http-port 9000 cmd` is a server-flag error as before). |

Manual check: `url_shortener cmd --help`.

## Constraints

- C4: `link` and server parsing are unchanged byte for byte (their tests are not edited).
- C5: parsing never starts a listener or reads state.
- Untrusted input: argv text is carried verbatim; length and JSON validity are checked later by the envelope/registry layer (`max_request_body_bytes`).
- C++17; Boost.ProgramOptions is not used for this branch (same as `link`); naming per `.clang-tidy`.

## Success criteria

1. `cli__07_cli_parser_cmd` passes; `cli__01..04` unchanged and passing.
2. `ParseResult::command` and `ParseResult::cmd` are never both engaged (test).
3. `url_shortener cmd --help` exits 0 with the usage text.

## Out of scope / risks

- Per-command `--flag` sugar for `cmd` (use `link <verb>` or `--arg`).
- Exit code of parse errors stays 1 (shared `main()` catch); scripts should branch on stdout/JSON, not on usage errors. Changing `main`'s error exit code is a separate decision (it would alter `link` behavior, C4).
