# 17 - serve-stdio runtime wiring (process setup and adapter run)

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/12 (transport factory), 05.0/15 (`StdioAdapter`), 05.0/16 (parse result), 05.0/20 (`build_command_runtime` with the process-wide job runtime)
**Blocks:** 18
**Estimated size:** S
**Suggested agent:** cpp-expert (security review: security-auditor)

## Objective

Make `url_shortener serve-stdio` run end to end: obtain the command runtime from the one composition root
(`composition::build_command_runtime`, Task 03.0 subtask 18 extended by subtask 20; plan C11), create the stdio
adapter through the factory, set up the process so stdout carries nothing but protocol lines, run the adapter,
shut the process-wide job runtime down and return the exit code. **This subtask builds no registry, no service
bundle and no `JobRunner` of its own.** The dispatch lives in a new `src/cli/serve_stdio_dispatch.cpp`
(mirroring `link_command_dispatch.cpp`) so `main.cpp` only gains a three-line branch.

## Dependencies

- 16 (parse result incl. `--read-only`, `--no-file-paths`, `--path-root`), 12/15 (factory and adapter), 20
  (`build_command_runtime(config, CommandRuntimeOptions{file_paths})`, `CommandRuntime::job_runner`,
  `shutdown_process_job_runtime`).
- The configured backend comes from Task 02.0: `main` has already called `InitLinkBackend` (02.0/12) before
  dispatching to this mode, so the runtime sees the same store as every other mode.
- Verified pattern: `src/cli/link_command_dispatch.cpp` (`DispatchLinkCommand`) must never include `http/` or
  `boost/asio` headers (C4/C5); the same rule applies here.
- Verified: `LoggerFactory::Initialize(const LoggingConfig&, std::string*)`; `LoggingConfig::sink` defaults to
  `LogSink::Stderr`, but `LogSink::Stdout` exists and the `Logger` writes to `std::cout` for it
  (`src/observability/logger.cpp`), which would corrupt the NDJSON stream. An uninitialized logger is enabled
  with the `Stderr` sink (`logger_factory.cpp`), which is acceptable here.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/cli/serve_stdio_dispatch.hpp` | create | `RunServeStdio` declaration |
| `src/cli/serve_stdio_dispatch.cpp` | create | process setup, runtime lookup, run, shutdown |
| `src/main.cpp` | modify | `if (parsed.serve_stdio) return cli::RunServeStdio(parsed.config, *parsed.serve_stdio);` right after the `parsed.command` branch |
| `sources.cmake` | modify | add `src/cli/serve_stdio_dispatch.cpp` to `COMMON_CPP_SOURCES`; the test to `CLI_UNIT_SOURCES` |
| `tests/unit/cli/21_serve_stdio_dispatch.cpp` | create | in-process tests with injected streams (Task 05.0 owns `tests/unit/cli/20-29`) |

## Classes and functions

```cpp
namespace url_shortener::cli {
// Testable seam: streams injected, no process-global side effects other than the job runtime.
int RunServeStdioOn(const ServerConfig& config, const ServeStdioCliOptions& options,
                    std::istream& in, std::ostream& out);
// Process entry: applies platform setup, then RunServeStdioOn(std::cin, std::cout).
int RunServeStdio(const ServerConfig& config, const ServeStdioCliOptions& options);
}
```

`RunServeStdio` setup, in order, before the first read or write:

1. `std::ios::sync_with_stdio(false); std::cin.tie(nullptr);`
2. Windows: `_setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);` (`<io.h>`, `<fcntl.h>`, `#ifdef _WIN32`) so `\n` is not rewritten to `\r\n` and Ctrl-Z does not end input. POSIX: `std::signal(SIGPIPE, SIG_IGN)` so a closed stdout becomes a stream failure (exit 4) instead of a kill.
3. Logging: `LoggerFactory::Initialize(cfg)` with `cfg.sink = LogSink::Stderr` forced regardless of any configuration (a `File` sink is allowed only if explicitly configured by a later flag; `Stdout` is never allowed in this mode). Log lines, including the audit records of the runtime's `LoggerAuditSink`, go to stderr only.

`RunServeStdioOn`:

1. `--path-root`, when given, must name an existing directory (else exit 2 with one stderr line, before reading input).
2. `auto runtime = composition::build_command_runtime(config, CommandRuntimeOptions{ .file_paths = {!options.no_file_paths, options.path_root} })`
   (C++17: assign the members, no designated initializers).
3. `TransportOptions{ read_only = options.read_only, max_line_bytes = config.max_request_body_bytes, drain_timeout = 30 s, job_runner = runtime.job_runner }`.
4. `auto adapter = composition::make_transport_adapter("stdio", transport_options, in, out); int rc = adapter->run(*runtime.registry);`
5. `composition::shutdown_process_job_runtime(std::chrono::seconds(5))` (the adapter already drained; this joins the workers); return `rc`.

The file must not include anything under `url_shortener/http/` or `boost/asio`, must not construct `CommandRegistry`,
`JobRunner` or a `LinkCommandService` bundle, and must not touch `UriMapSingleton`/`uri.txt` (C5, C11).

## Implementation steps

1. Write the header/source with the two functions and the `main.cpp` branch.
2. Add a startup log line `serve_stdio_start` (version, read_only, file-path policy, backend *names* only) and `serve_stdio_stop` (exit code, responses written), both to stderr; no request data.
3. Write the in-process test using `RunServeStdioOn` with `std::stringstream` and an in-memory config.
4. Add the grep guards (below) and run them.
5. Manually run the binary once on the host platform (see Commands).

## Commands

```text
cmake --build cmake-build --target url_shortener cli__21_serve_stdio_dispatch
ctest --test-dir cmake-build -R "^cli__21_serve_stdio_dispatch$" --output-on-failure
```
Manual smoke (PowerShell; the binary path depends on the generator, `cmake-build\url_shortener.exe` or `cmake-build\Debug\url_shortener.exe`):

```powershell
'{"v":1,"id":"a","command":"command.list"}' | .\cmake-build\url_shortener.exe serve-stdio
```
Bash: `echo '{"v":1,"id":"a","command":"command.list"}' | cmake-build/url_shortener serve-stdio`. Windows PowerShell 5.1 pipes text with a BOM-less encoding only if `$OutputEncoding` is UTF-8; if the first response is `invalid_envelope`, set `$OutputEncoding = [Text.UTF8Encoding]::new($false)` (the reader also strips a leading BOM).

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/cli/21_serve_stdio_dispatch.cpp` | `cli__21_serve_stdio_dispatch` | unit | `command.list` equals the list produced by `build_command_runtime` for the same config (same names, same order: no mode-specific registration); `link.create` then `link.get` in one session returns the same link; `link.import` inline CSV returns a `job`, a later `job.get` in the same session reaches `succeeded`; `db.tables` lists `links`, `click_events`, `jobs`; read-only option: `link.create` -> `not_authorized`; `--no-file-paths`: `link.import` with `path` -> `unsupported`; `--path-root <tmp>`: a `path` outside it -> `invalid_field`, inside it -> accepted; nonexistent `--path-root` -> exit 2 before reading input; logging never writes to the injected `out` (logger sink forced to stderr: assert via captured `std::cerr` rdbuf swap and empty extra output); no worker thread survives `RunServeStdioOn` (process-wide runtime shut down; `reset_process_job_runtime_for_tests` in teardown) |

Manual check: run the smoke command on Windows and Linux; confirm one JSON line on stdout, nothing else, exit code 0; `echo $?` / `$LASTEXITCODE`.

## Constraints

- C5: no `HttpServer`, no sockets, no `uri.txt`, no `boost/asio` include in `serve_stdio_dispatch.cpp` (grep guard).
- C11: no registry, runner or service construction here (grep guard).
- C6: stdout carries envelopes only; logs to stderr; startup log prints backend names only; never print config values containing key/dsn/secret/password/token (`::observability::redactSecretValue` rules).
- C7: local operator = Admin unless `--read-only`; the auth broker is untouched.
- C9/C++17; naming/format; `.cpp` in `sources.cmake`; C10 untouched.

## Success criteria

1. `cli__21_serve_stdio_dispatch` passes.
2. `grep -rn "http/\|boost/asio\|UriMapSingleton\|uri.txt" src/cli/serve_stdio_dispatch.cpp include/url_shortener/cli/serve_stdio_dispatch.hpp` returns nothing.
3. `grep -n "CommandRegistry \|JobRunner \|BuildLegacyLinkCommandService" src/cli/serve_stdio_dispatch.cpp` returns nothing.
4. The manual smoke command prints exactly one valid JSON line and exits 0 on Windows and Linux.
5. Running with `--read-only` rejects a write command with `not_authorized` (also covered by 18).

## Out of scope / risks

- Stdio tuning flags (`--drain-timeout`, worker count): defaults only.
- Persistence across processes depends on `--storage-config` (Task 02.0); without it `serve-stdio` sees only per-process in-memory data (same as today's one-shot CLI); the integration tests in 18 skip persistence assertions in that case.
