# 12 - main.cpp wiring order

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/08, 02.0/09, 02.0/11
**Blocks:** 13, 14, 03.0/18, 05.0/20, 06.0/19
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Call `InitLinkBackend` from `main()` at the one point where it is correct for
every mode: after argument parsing and `--help`, before the CLI dispatch and
before `HttpServer` is constructed, so the redirect handlers, REST handlers and
the CLI all see the same installed store. Flush pending redirect counters on
the way out. Map startup storage errors to a clean stderr message and a stable
exit code in CLI mode. No change when `--storage-config` is absent.

## Dependencies

- 02.0/11 `InitLinkBackend`, `LinkBackendHandle`; 02.0/08-09 (handlers already
  use the holder, otherwise the server would ignore the installed store).
- Current `main()` structure (verified, `src/main.cpp`): parse -> `--help`
  return -> `if (parsed.command.has_value()) return DispatchLinkCommand(...)` ->
  logger init -> `net::io_context` -> `HttpServer server(io_context, config)` ->
  `uri.txt` load -> `server.run()` -> signal handler (`server.stop()`,
  `UriMapSingleton::serialize`, `io_context.stop()`) -> `io_context.run()`;
  the outer `catch (const std::exception&)` logs `unhandled_exception` and
  returns 1.
- Exit-code vocabulary (verified, `ExitCodeForAppError`): 4 = storage/internal.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `src/main.cpp` | modify | Include bootstrap header; construct handle; flush on exit |
| `tests/integration/cli/17_storage_config_errors.py` | create | Startup-error and ordering tests |
| `sources.cmake` | modify | `CLI_INTEGRATION_SOURCES` += test (ctest name `cli_17_storage_config_errors`) |

## Classes and functions

New control flow in `main` (sketch; only the marked lines are new):

```cpp
CliParser cli;
ParseResult parsed = cli.parse(argc, argv);
if (parsed.help_requested) { ...unchanged... }

// NEW: storage backend, before anything can serve or execute a link command.
url_shortener::app::LinkBackendHandle backend;
try {
    backend = url_shortener::app::InitLinkBackend(parsed.config);
} catch (const std::runtime_error& e) {
    std::cerr << e.what() << '\n';          // message already free of secrets (11)
    return url_shortener::cli::ExitCodeForAppError(url_shortener::app::AppErrorCode::storage_failure); // 4
}

if (parsed.command.has_value()) {
    const int rc = url_shortener::cli::DispatchLinkCommand(*parsed.command, parsed.config);
    backend.flush();                         // NEW (no-op for the default path)
    return rc;
}

auto& config = parsed.config;
LoggerFactory::Initialize({});
auto logger = LoggerFactory::Component("main");
if (backend.active()) { logger.info("storage_backend", {{"summary", backend.summary()}}); } // NEW
... io_context, HttpServer server(io_context, config) ... unchanged ...
// in the signal handler, after server.stop():
// (Task 05.0 subtask 20 inserts the job-runtime drain here, before the flush)
backend.flush();                              // NEW, before io_context.stop()
```

`backend.flush()` is a shutdown-only call; during normal operation the
counters are persisted by the store's background flusher (02.0/05, started by
02.0/11), never by a request.

Ordering rules, each tested or grep-checked:

1. `backend` is declared before `io_context` and `server`, so it is destroyed
   after them (reverse construction order) and the destructor's final flush runs
   after the server stopped.
2. The CLI branch still returns before any `io_context`/`HttpServer`/`uri.txt`
   code (milestone C5): `InitLinkBackend` opens only the configured database.
3. `--help` and flag-parse errors never reach `InitLinkBackend` (no file I/O).
4. Logging is initialized only in server mode, as today; CLI mode prints
   nothing to stdout from the bootstrap (stdout carries only the JSON result).
   Note (verified, `logger_factory.cpp`): an uninitialized logger is
   *enabled* with a `stderr` sink, so modes that must keep the console clean
   (`tui`) initialize it explicitly (Task 06.0 subtask 19).
5. Without `--storage-config` (and env), `backend` stays inactive and the
   program path is identical to today.

## Implementation steps

1. Edit `main.cpp` as sketched; include
   `<url_shortener/composition/link_backend_bootstrap.hpp>` and
   `<url_shortener/cli/link_command_dispatch.hpp>` (already included).
2. Build the binary.
3. Write the Python integration test; register.
4. Run manual smoke: default CLI create; SQLite CLI create + get; server start
   with a bad config.

## Commands

```powershell
cmake --build cmake-build --target url_shortener
ctest --test-dir cmake-build -R "^cli_" --output-on-failure           # whole CLI integration family
ctest --test-dir cmake-build -R "^cli_17_storage_config_errors$" --output-on-failure
# manual
$cfg = Join-Path $env:TEMP "us_storage.yaml"
"metadata:`n  backend: sqlite`n  sqlite_path: $($env:TEMP -replace '\\','/')/us.db" | Out-File -Encoding ascii $cfg
./cmake-build/url_shortener link create --url https://example.com --slug manual1 --storage-config $cfg
./cmake-build/url_shortener link get --slug manual1 --storage-config $cfg
```

(Visual Studio generators: binary under `cmake-build/Debug/`. Use forward
slashes in the YAML path to avoid escaping issues. The `ctest` names for CLI
integration tests are `cli_<NN>_<name>` per `CMakeLists.txt` lines 257-269; the
`SIMPLE_HTTP_BIN` environment variable is set by CTest.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/integration/cli/17_storage_config_errors.py` | `cli_17_storage_config_errors` | integration | below |
| all existing `cli_*` integration tests | unchanged | integration | must pass unmodified (default mode) |

- `link get --slug x --storage-config <missing file>` -> exit 4, stdout empty,
  stderr starts with `storage config:`.
- Invalid backend name in YAML -> exit 4, stderr names the backend, not the file.
- SQLite path in a nonexistent directory -> exit 4, stderr
  `storage backend unavailable`.
- `--help` and `link --help` with a bogus `--storage-config` path -> exit 0 (no
  file access), proving ordering rule 3.
- Server mode with a bad storage config (`url_shortener --storage-config <missing>`)
  exits non-zero within a few seconds and **does not** open its HTTP port
  (reuse the port probe of `04_cli_exits_no_listening_port.py`).
- A successful `link create ... --storage-config <good sqlite yaml>` prints
  exactly one JSON line to stdout and nothing from the bootstrap (ordering
  rule 4) and does not bind port 8000.
- Without the flag the existing 16 CLI integration tests are unchanged.

## Constraints

- C5: no listener in CLI mode; `main.cpp` CLI branch must not gain `HttpServer`
  or `uri.txt` code (grep: `link_command_dispatch.cpp` still has no
  `url_shortener/http/` include).
- No secrets in output: only `e.what()` of the bootstrap (sanitized there) is
  printed.
- Server shutdown path must keep working for the default case (SIGINT test in
  `10_server_mode_unchanged_by_cli.py`).
- Windows: `SIGHUP` branch untouched; `flush()` added in the portable handler
  body.

## Success criteria

1. `cli_17_storage_config_errors` passes; all other `cli_*` tests still pass.
2. Manual smoke: the two commands above print the created and the fetched link
   (same `id`), run as separate processes.
3. `grep -n "InitLinkBackend" src/main.cpp` shows the call before the
   `HttpServer` construction and before `DispatchLinkCommand`.

## Out of scope / risks

- Future modes (`cmd`, `tui`, `serve-stdio`) call the same bootstrap at the same
  place (Tasks 04.0-06.0); this subtask does not add them.
- `SIGHUP` reload of the storage configuration is not supported (TLS context
  reload only); document that storage config changes need a restart.
- Mapping all bootstrap failures to exit 4 loses the "bad input" (2) vs
  "unavailable" (4) distinction; refine with two exception types only if
  scripts need it.
