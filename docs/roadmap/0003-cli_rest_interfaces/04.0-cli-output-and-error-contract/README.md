# Task 04.0 - CLI output and error contract

**Parent milestone:** [Milestone 0003 - CLI & REST Command Interfaces](/docs/roadmap/0003-cli_rest_interfaces/plan.md)
**Status:** ⬜ Not started
**Depends on:** [03.0 CLI dispatch and process lifecycle](/docs/roadmap/0003-cli_rest_interfaces/03.0-cli-dispatch-and-lifecycle/README.md)

## Scope

Without a shared convention, each of the ten `link <verb>` commands could
end up with subtly different success/error output. This task defines that
convention once and applies it uniformly:

- **Success:** a single-line JSON object on stdout, exit code `0`. For
  commands returning a `LinkView`/`LinkStatsView`, reuse
  `app::serializeLinkViewJson`/`app::serializeLinkStatsJson` verbatim - do
  not write a second serializer.
- **Error:** a non-zero exit code plus a human-readable message on stderr,
  derived from the same `AppErrorCode` the REST adapter already maps via
  `statusForAppError`/`codeForAppError` in `link_handlers.cpp` - mapped to
  exit codes/text instead of HTTP status/JSON error envelope.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Success envelope reuse](01-success-envelope-reuse.md) | ✅ Complete | 02 |
| 02 | [Error envelope and exit codes](02-error-envelope-and-exit-codes.md) | ⬜ Not started | none |

## Key constraints

- No new JSON serialization path for data already covered by
  `serializeLinkViewJson`/`serializeLinkStatsJson`.
- `AppErrorCode -> exit code` mapping is defined once in one place and used
  by every `link <verb>` command, not re-derived per command.
- stdout carries only the success payload; all diagnostic/error text goes to
  stderr, so scripting against stdout JSON is reliable.
