# Task 03.0 - CLI dispatch and process lifecycle

**Parent milestone:** [Milestone 0003 - CLI & REST Command Interfaces](/docs/roadmap/0003-cli_rest_interfaces/plan.md)
**Status:** ⬜ Not started
**Depends on:** [02.0 CLI argument parsing](/docs/roadmap/0003-cli_rest_interfaces/02.0-cli-argument-parsing/README.md)

## Scope

`src/main.cpp` currently has exactly one branch (`parsed.help_requested`);
otherwise it unconditionally builds `HttpServer`, loads `uri.txt` into
`UriMapSingleton`, and calls `io_context.run()`. This task adds the second
branch: when `ParseResult` carries a recognized `link <verb>` command, the
process must construct the same `LinkCommandService` the HTTP handlers use
(via `BuildLegacyLinkCommandService`/`makeCommandService`'s equivalent),
invoke the resolved method, and exit - **without** constructing
`boost::asio::io_context`, `HttpServer`, or touching `UriMapSingleton`/
`uri.txt` at all.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [main.cpp branch before server construction](01-main-dispatch-before-server-construction.md) | ✅ Complete | 02, 03 |
| 02 | [CLI storage bootstrap](02-cli-storage-bootstrap.md) | ✅ Complete | 03 |
| 03 | [Exit code and process lifetime guarantees](03-exit-code-and-process-lifetime.md) | ⬜ Not started | none |

## Key constraints

- C4/C5 from `plan.md`: no listener is ever bound, `io_context.run()` is
  never called, and `uri.txt`/`UriMapSingleton` are never touched on the
  `link <verb>` path.
- The branch must sit before `net::io_context io_context;` in `main.cpp`,
  not after a partially-constructed server is torn down - this is a
  short-circuit, not a "start and immediately stop" sequence.
- Signal handling (`SIGINT`/`SIGTERM`/`SIGHUP`) registered later in
  `main.cpp` is irrelevant to the CLI path and must not be set up for it.
- Command dispatch reuses the exact `LinkCommandService` construction
  handlers use today, so CLI and REST observe the same
  `linkRepository()` singleton state within a single process invocation
  (per-invocation, not shared across separate CLI process runs beyond what
  the singleton already persists in-process).
