# 19 - `tui` CLI mode wiring (parser, main, entry point)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/01, 02, 04, 05; 02.0/10 (`--storage-config` flag, `extract_storage_config_flag`), 02.0/12 (`main.cpp` backend bootstrap order), 03.0/18 (`composition::build_command_runtime`), 05.0/20 (process-wide job runtime and its shutdown)
**Blocks:** 06.0/20 (all screens plug in independently of this, but the app cannot be launched without it)
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Add `url_shortener tui [--storage-config <file>] [--read-only] [--yes] [--theme dark|light|mono] [--log-file <path>]`
(ADR 0003 "Launching"). The mode must follow the one-shot CLI precedent in
`src/main.cpp`: it short-circuits **before** any `io_context`, `HttpServer`,
signal set or `uri.txt` load (contract C5), obtains its registry from the one
composition root `composition::build_command_runtime` (plan C11; the same
function `cmd`, `serve-stdio` and the REST bridge use), wraps it in
`InProcessClient`, and runs `App`. With `-DURLSHORTENER_ENABLE_TUI=OFF` the
word `tui` still parses and fails with a clear message.

## Dependencies

- Verified existing code: `src/main.cpp` (`parsed.command` branch calls `url_shortener::cli::DispatchLinkCommand` and returns before `LoggerFactory::Initialize`, `HttpServer`, `UriMapSingleton`); `src/cli_parser.cpp` (the `argc >= 2 && std::string(argv[1]) == "link"` branch near the end of shared-config handling, ~line 383); `include/url_shortener/cli/cli_parser.h` (`ParseResult` with `config`, `help_requested`, `help_text`, `command`); `include/url_shortener/cli/link_command_dispatch.hpp` (`ExitCodeForAppError`, mapping 1/2/3/4).
- Storage: Task 02.0 subtask 10 adds `ServerConfig::storage_config_path`, the `--storage-config` flag, the env fallback `URL_SHORTENER_STORAGE_CONFIG` and the reusable file-local helper `extract_storage_config_flag(std::vector<std::string>&, ServerConfig&)`; the `tui` branch calls that helper (no second flag parser). Task 02.0 subtask 12 runs `InitLinkBackend` in `main` before any mode dispatch; the `tui` branch is placed **after** that call, so the TUI sees the configured backend.
- Registry: `composition::build_command_runtime(config)` (03.0/18) with the process-wide job runtime of 05.0/20 (`runtime.job_runner`, `shutdown_process_job_runtime`).
- Verified (`src/observability/logger_factory.cpp`): the global `LoggingConfig g_cfg{}` defaults to `enabled = true`, level `Info`, sink `Stderr`, so a logger that was never initialized **still writes to stderr**; the runtime's `LoggerAuditSink` (03.0/18) would therefore corrupt the screen unless logging is initialized explicitly.
- `ServerConfig::max_request_body_bytes` (verified, default 65536) is the inline limit.
- External: Boost.ProgramOptions (already used), FTXUI via `url_shortener_tui`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/cli/cli_parser.h` | modify | `struct TuiCliOptions { bool read_only=false; bool assume_yes=false; std::string theme="dark"; std::optional<std::string> log_file; };` and `std::optional<TuiCliOptions> tui;` in `ParseResult` (no FTXUI include); the storage path goes to `ParseResult::config.storage_config_path` like every mode |
| `src/cli_parser.cpp` | modify | `if (argc >= 2 && std::string(argv[1]) == "tui")` branch beside the `link` branch; calls `extract_storage_config_flag`; own `boost::program_options` description; `tui --help` text; theme validated against `dark|light|mono` |
| `include/url_shortener/tui/tui_entry.hpp` | create | `int run_tui(const TuiCliOptions&, const ServerConfig&);` (declaration only; no FTXUI types) |
| `src/tui/tui_entry.cpp` | create | wiring (UI lib): logging init -> `build_command_runtime` -> `InProcessClient` -> `App::run` -> job runtime shutdown |
| `src/main.cpp` | modify | branch after the backend bootstrap and the `link`/`cmd` branches: `#if URLSHORTENER_WITH_TUI` call `tui::run_tui`; `#else` print "built without TUI support (URLSHORTENER_ENABLE_TUI=OFF)" to stderr, return 2 |
| `include/url_shortener/tui/tui_options.hpp` | modify | `TuiOptions { ThemeName theme; bool read_only; bool assume_yes; std::size_t max_inline_bytes = 65536; }` is created in 04 (so `App` compiles); this subtask only adds the mapping from `TuiCliOptions` |
| `tests/unit/cli/30_cli_parser_tui_mode.cpp` | create | parser tests (runs even when TUI OFF); append to `CLI_UNIT_SOURCES` (Task 06.0 owns `tests/unit/cli/30-39`) |
| `tests/unit/tui/19_tui_entry_boundary_test.cpp` | create | static check: `tui_entry.cpp` and `main.cpp` tui branch include no `http_server.h`, `uri_map_singleton.h`, no `boost/asio`, and construct no `CommandRegistry` |
| `tests/unit/tui/19_tui_options_test.cpp` | create | option mapping, logging config derivation |
| `docs/cli/README.md` | modify | add `tui` synopsis (small; the full guide is Task 08.0 subtask 10) |
| `sources.cmake` | modify | add `tui_entry.cpp` (UI list), tests |

## Classes and functions

```cpp
// cli_parser.h (existing file, additive)
struct TuiCliOptions { bool read_only = false; bool assume_yes = false; std::string theme = "dark";
                       std::optional<std::string> log_file; };

namespace url_shortener::tui {
int run_tui(const TuiCliOptions& cli, const ServerConfig& config);
// exit codes: 0 normal quit; 2 usage/not a terminal/built without TUI; 4 storage/bootstrap failure (matches cli::ExitCodeForAppError grouping)
std::optional<TuiOptions> to_tui_options(const TuiCliOptions& cli, const ServerConfig& config, std::string* error);  // validates theme via parse_theme_name
/// Logging for TUI mode (decision log D14): always sink=File - at cli.log_file when given, otherwise
/// at the default `url_shortener-tui.log` in the current directory (append). Never Stdout/Stderr,
/// never disabled, so every mutation's audit line is kept.
observability::LoggingConfig tui_logging_config(const TuiCliOptions& cli);
}
```

Behaviour of `run_tui`:

1. Refuse when stdin or stdout is not a terminal (`_isatty(_fileno(stdin))` on Windows, `isatty(STDIN_FILENO)` on POSIX; no FTXUI dependency) with a one-line stderr message and exit 2.
2. **Initialize logging explicitly** with `LoggerFactory::Initialize(tui_logging_config(cli))` *before* building the runtime: `File` sink at `--log-file <path>` or, by default, `./url_shortener-tui.log` (D14: audit lines of TUI mutations must land somewhere durable, never on the screen). If the file cannot be opened, refuse to start with a one-line stderr message and exit 2. The status bar shows the log path once at start. Not calling `Initialize` is not enough (verified default: enabled, stderr).
3. `auto runtime = composition::build_command_runtime(config);` `CallerContext{principal="local-operator", ControlSet::Admin or User if --read-only, transport="tui"}`; `InProcessClient client(*runtime.registry, caller)`.
4. Run `App`; on quit call `composition::shutdown_process_job_runtime(std::chrono::seconds(5))` (running imports are cancelled cooperatively; partial results kept, catalog section 4) before returning.
5. Print nothing to stdout after the alt-screen closes except errors; restore the terminal even on exceptions (RAII guard + `catch (...)`).

## Implementation steps

1. Parser: add the branch **after** shared-config env handling (same place as `link`), before server option parsing, so server flags never mix; call `extract_storage_config_flag`; unknown flags -> `std::invalid_argument` (same convention as `link`).
2. `tui --help` text lists the flags and that no server is started.
3. `main.cpp`: add the `tui` branch right after the `parsed.command`/`cmd` branches (i.e. after `InitLinkBackend` and before `net::io_context`, `HttpServer`, `uri.txt`). Keep the diff to ~8 lines.
4. Implement `to_tui_options`, `tui_logging_config`, `run_tui`, `tui_entry.cpp`; `max_inline_bytes` from `ServerConfig::max_request_body_bytes`.
5. Register all screens in `tui_entry.cpp` (the only place that names every screen class).
6. Verify C5 manually and with the static test (Tests below).

## Commands

```powershell
cmake --build cmake-build --target url_shortener tui__19_tui_options_test tui__19_tui_entry_boundary_test cli__30_cli_parser_tui_mode
ctest --test-dir cmake-build -R "^(cli__30_|tui__19_)" --output-on-failure
.\cmake-build\url_shortener.exe tui --help
.\cmake-build\url_shortener.exe tui --theme mono --read-only
# prove no listener / no uri.txt (PowerShell, empty working dir)
$exe = (Resolve-Path .\cmake-build\url_shortener.exe).Path
New-Item -ItemType Directory -Force $env:TEMP\tui-c5 | Out-Null; Push-Location $env:TEMP\tui-c5
$p = Start-Process -FilePath $exe -ArgumentList "tui" -PassThru      # opens its own console window
Start-Sleep -Seconds 3
Get-NetTCPConnection -State Listen -OwningProcess $p.Id -ErrorAction SilentlyContinue   # expect no rows
Test-Path .\uri.txt                                                                      # expect False
Stop-Process -Id $p.Id; Pop-Location
```

(Target names for tests under `tests/unit/cli/` follow `<parent_dir>__<stem>`: `cli__30_cli_parser_tui_mode`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/30_cli_parser_tui_mode.cpp` | `cli__30_cli_parser_tui_mode` | unit | `tui` alone -> `ParseResult.tui` set, `command` empty, `help_requested` false; each flag parsed; `tui --storage-config /x.yaml` sets `config.storage_config_path` (same helper as `link`); `--theme solarized` -> `invalid_argument`; `tui --help` sets help text; unknown flag rejected; `tui` combined with server flags (`--http-port`) rejected; existing `link get --slug x` and server-mode parse results unchanged (regression, byte-compare help text) |
| `tests/unit/tui/19_tui_options_test.cpp` | `tui__19_tui_options_test` | unit | `to_tui_options` mapping for each theme; default values; `max_inline_bytes` propagation from `ServerConfig`; `tui_logging_config`: default -> `enabled == true`, `sink == File`, path `url_shortener-tui.log`; `--log-file f` -> `sink == File`, path `f`; never `Stdout`/`Stderr`, never disabled |
| `tests/unit/tui/19_tui_entry_boundary_test.cpp` | `tui__19_tui_entry_boundary_test` | unit | `tui_entry.cpp`, `app.cpp`, `main.cpp` text contains none of `HttpServer`, `http_server.h`, `UriMapSingleton`, `uri.txt`, `io_context` inside the tui branch (regex over the tui branch region delimited by markers `// tui-mode-begin` / `// tui-mode-end` that this subtask adds in `main.cpp`); `tui_entry.cpp` contains `build_command_runtime` and no `CommandRegistry registry`/`JobRunner` construction |

Manual: [20](/docs/roadmap/0006-unified-command-model-and-tui/06.0-tui/20-manual-smoke-checklist.md) items on listener/`uri.txt`, non-TTY refusal (`url_shortener tui < NUL`), OFF build message, no stray log text on screen after a create (the audit line is in `url_shortener-tui.log` or the `--log-file` path).

## Constraints

- C5: `tui` never constructs `HttpServer`, binds a socket, or touches `uri.txt`/`UriMapSingleton`.
- C4: existing `link <verb>` and server-mode parsing/help stay byte-identical (regression test).
- C8: `tui_entry.cpp` is one of the two files allowed to see the registry (with `in_process_client.cpp`).
- C11: no registry, runner or service construction in the TUI; only `build_command_runtime`.
- C9: no new dependency; CLI parsing remains Boost.ProgramOptions; new `.cpp` in `sources.cmake`; TUI code compiled only under `URLSHORTENER_ENABLE_TUI`, parser changes always.
- The storage-config flag is the one defined by Task 02.0 subtask 10, never duplicated.

## Success criteria

1. `url_shortener tui --help` prints usage and exits 0 without a terminal.
2. `url_shortener tui` in an interactive terminal shows the D1 frame; quitting returns exit code 0 and restores the terminal.
3. During the session there is no listening socket and no `uri.txt` is created (manual + static test).
4. A build with `-DURLSHORTENER_ENABLE_TUI=OFF` compiles; `url_shortener tui` exits 2 with "built without TUI support".
5. Existing CLI parser tests (`tests/unit/cli/01-04`) still pass unchanged.
6. Creating a link in the TUI writes no text to the terminal outside FTXUI, and exactly one `command_audit` line to the log file.

## Out of scope / risks

- Windows `--no-color`, `NO_COLOR` env handling (nice-to-have; mono theme covers it), remote client flags (`--remote <url>`).
- Risk: if Task 02.0 slips, `run_tui` still works against the default in-memory backend (banner "memory store").
