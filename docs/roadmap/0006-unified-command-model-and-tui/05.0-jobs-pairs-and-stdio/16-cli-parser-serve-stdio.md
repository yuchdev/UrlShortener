# 16 - CLI parser: `serve-stdio` mode

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** none (coordination only: Task 04.0 subtask 12 parses `cmd` in the same files)
**Blocks:** 17
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Teach `CliParser` to recognize `url_shortener serve-stdio [options]` as a one-shot mode, exactly as it already recognizes `link <verb>`: a leading positional short-circuits server-flag parsing and is returned in `ParseResult`, so `main` can dispatch before any `io_context`/`HttpServer` exists. Parsing only: nothing is executed in this subtask.

## Dependencies

- Verified: `CliParser::parse(int, char*[]) const` returns `ParseResult {config, help_requested, help_text, command}`; `src/cli_parser.cpp` checks `argc >= 2 && std::string(argv[1]) == "link"` (line ~383) before the Boost.ProgramOptions server parse, where the first positional is otherwise a port (`positional-port`).
- Coordination: Task 04.0 subtask 12 adds `cmd` to the same dispatch point and may restructure `ParseResult` (for example a mode enum). Land whichever comes first and adapt the other; this spec assumes the minimal additive field below.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/cli/cli_parser.h` | modify | `ServeStdioCliOptions`, `ParseResult::serve_stdio` |
| `src/cli_parser.cpp` | modify | `serve-stdio` branch next to the `link` branch; usage/help text |
| `tests/unit/cli/20_cli_parser_serve_stdio.cpp` | create | parser cases |
| `sources.cmake` | modify | add the test to `CLI_UNIT_SOURCES` (no new `.cpp`) |

## Classes and functions

```cpp
// cli_parser.h (global namespace like the existing LinkCliVerb / ParseResult)
struct ServeStdioCliOptions
{
    bool read_only = false;                 // --read-only: downgrade local caller to ControlSet::User
    bool no_file_paths = false;             // --no-file-paths: link.import/export `path` -> unsupported
    std::optional<std::string> path_root;   // --path-root <dir>: confine `path` to this directory (05.0/07 rule 6)
};
// ParseResult gains:
//   std::optional<ServeStdioCliOptions> serve_stdio;   // set only for `serve-stdio`
```

Accepted syntax:

| Invocation | Result |
|---|---|
| `serve-stdio` | `serve_stdio` set, defaults |
| `serve-stdio --read-only` | `read_only = true` |
| `serve-stdio --no-file-paths` | `no_file_paths = true` (decision Q6: recommended when the parent is a webview-driven sidecar) |
| `serve-stdio --path-root <dir>` | `path_root = <dir>`; the directory must exist (checked at startup in 17, not here); combining it with `--no-file-paths` throws `std::invalid_argument` |
| `serve-stdio --base-domain <d>` and other global flags that `link` verbs already accept | applied to `ParseResult::config` the same way as for `link` (reuse the helper; do not fork it). Which global flags exist is UNVERIFIED beyond `--base-domain`; check `src/cli_parser.cpp` when implementing. |
| `serve-stdio --help` / `-h` | `help_requested = true`, `help_text` = usage |
| `serve-stdio <extra positional>` or unknown flag | throws `std::invalid_argument` (same behavior as other unknown arguments; `main` maps it to its existing error path) |

Usage text lists the framing contract in two lines (NDJSON, EOF = shutdown) and points at `docs/commands/adding-a-transport.md`.

## Implementation steps

1. Add the struct and field to `cli_parser.h` with Doxygen comments in the file's existing style.
2. In `parse`, add `if (argc >= 2 && std::string(argv[1]) == "serve-stdio")` before the server-option parse and after the `link` branch; delegate to a new static helper `parse_serve_stdio(argc, argv)` in the anonymous namespace.
3. Reuse the global-flag parsing helper that the `link` branch uses for `--base-domain` (UNVERIFIED helper name); keep `argv[1] != "serve-stdio"` behavior byte-identical for server mode.
4. Add tests in the style of `tests/unit/cli/01_cli_parser_link_commands.cpp`.
5. Confirm `tests/unit/cli/01..04` still pass.

## Commands

```text
cmake --build cmake-build --target cli__20_cli_parser_serve_stdio
ctest --test-dir cmake-build -R "^cli__(0[1-4]|20)_" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`. (Target names for `tests/unit/cli` come from `register_labeled_cpp_tests(CLI_UNIT_SOURCES ...)`: `cli__<stem>`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/20_cli_parser_serve_stdio.cpp` | `cli__20_cli_parser_serve_stdio` | unit | bare `serve-stdio`; `--read-only`; `--no-file-paths`; `--path-root /tmp/x`; `--path-root` without value throws; `--path-root` + `--no-file-paths` throws; `--help` and `-h`; unknown flag throws; extra positional throws; `serve-stdio` as a non-first argument (`8080 serve-stdio`) is NOT treated as the mode; server mode `url_shortener 8080` and `link get ...` parse exactly as before (regression); `command` stays empty for `serve-stdio`; `serve_stdio` stays empty for `link` |

Manual check: `url_shortener serve-stdio --help` prints usage and exits 0.

## Constraints

- C4: `link <verb>` and server-mode parsing unchanged.
- C5: parsing must not construct a server or load `uri.txt`.
- C9: no new dependency; Boost.ProgramOptions only where the file already uses it.
- C++17, naming per `.clang-tidy`/`.clang-format`; no new `.cpp`.

## Success criteria

1. `cli__20_cli_parser_serve_stdio` passes and `ctest -R "^cli__0[1-4]_"` is still green.
2. `git diff --stat` shows changes only in the four listed files.
3. `url_shortener --help` output is unchanged except for a one-line mention of `serve-stdio` (reviewed manually).

## Out of scope / risks

- Execution of the mode (17), stdio-specific tuning flags (`--drain-timeout`), TUI mode flags.
- Risk: merge conflict with 04.0/12 in `cli_parser.cpp`; mitigate by landing them in sequence and reusing one dispatch helper.
- Test number: Task 05.0 owns `tests/unit/cli/20-29` (plan.md "Test file prefixes").
