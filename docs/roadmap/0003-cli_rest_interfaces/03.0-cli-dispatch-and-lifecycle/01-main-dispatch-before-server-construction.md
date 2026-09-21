# 01 - main.cpp branch before server construction

**Parent task:** 03.0 CLI dispatch and process lifecycle
**State:** ⬜ Not started
**Depends on:** none (Task 02.0 must land first for `ParseResult::command` to exist)
**Blocks:** 02, 03

## Objective

Add the branch in `src/main.cpp` that short-circuits to CLI dispatch when
`ParseResult::command.has_value()`, placed before
`net::io_context io_context;` is constructed and before the `uri.txt`
load - not merely before `server.run()`.

## Files to modify

- `src/main.cpp`:

```cpp
CliParser cli;
ParseResult parsed = cli.parse(argc, argv);

if (parsed.help_requested) {
    std::cout << parsed.help_text;
    return 0;
}

if (parsed.command.has_value()) {
    return url_shortener::cli::DispatchLinkCommand(*parsed.command, parsed.config);
}

// existing server-mode path unchanged from here down
```

- `include/url_shortener/cli/link_command_dispatch.hpp` (new) /
  `src/cli/link_command_dispatch.cpp` (new) - `DispatchLinkCommand` builds
  the command service (subtask 02), invokes the verb-appropriate
  `LinkCommandService` method based on `LinkCliCommand::verb`, and formats
  output (Task 04.0's job - this subtask can stub output formatting behind
  a placeholder if Task 04.0 has not landed yet, but should not block on
  it structurally).

## Tests

- `tests/unit/cli/` - a dispatch-level test asserting `DispatchLinkCommand`
  is reachable and returns without constructing any `HttpServer`/
  `io_context` symbol (a compile-time/link-time argument: `link_command_dispatch.cpp`
  must not `#include <url_shortener/http/http_server.h>` at all - enforce
  with a simple grep-based test or a note in the header's constraints if a
  runtime test is impractical).
- E2E (Task 05.0 subtask 04) provides the strongest evidence: an actual
  process run of `link create ...` followed by a port check.

## Constraints

- C4 (`plan.md`): this branch must be unconditionally reachable before any
  `HttpServer`, `io_context`, or `UriMapSingleton` symbol is touched.
- Signal-handling setup (`SIGINT`/`SIGTERM`/`SIGHUP`) stays below this
  branch, unreachable from the CLI path.

## Success criteria

- [ ] `main.cpp`'s CLI branch appears before `net::io_context io_context;`.
- [ ] `link_command_dispatch.cpp` has no dependency on `HttpServer`/
      `io_context`/`UriMapSingleton`.
- [ ] Server-mode behavior (no `link` token) is unchanged.
