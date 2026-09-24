# 03 - Exit code and process lifetime guarantees

**Parent task:** 03.0 CLI dispatch and process lifecycle
**State:** ✅ Complete
**Depends on:** 02
**Blocks:** none

## Objective

Guarantee every `link <verb>` invocation exits promptly with a code
reflecting success/failure (final mapping owned by Task 04.0), and never
leaves a listening socket or background thread running - extending the
existing `13_cli_no_server_socket.sh` pattern to all ten commands instead of
just `create`.

## Files to modify

- `src/cli/link_command_dispatch.cpp` - ensure `DispatchLinkCommand` returns
  an `int` suitable for `main`'s return value directly (no
  `std::exit`/`abort` calls that would bypass RAII cleanup of the storage
  bundle).

## Tests

- E2E: reuse `tests/e2e/scripts/sections/13_cli_no_server_socket.sh`'s port
  check for `create` and confirm it in Task 05.0 subtask 04's new sections
  for `update`/`delete`/`enable`/`disable`/`restore`/`preview`.
- Timing: assert process exit within a couple of seconds for each command,
  matching the existing manual QA expectation
  (`docs/testing/cli_link_commands.md` test case 10).

## Constraints

- No command path may leave any thread running past `main`'s return (the
  legacy `linkRepository()` singleton and `LegacyLinkStore` are synchronous,
  so this should hold naturally - confirm rather than assume once
  `AnalyticsWorker`/background-thread-owning code is anywhere on the CLI
  path, since analytics/queue infrastructure explicitly must never affect
  latency or block shutdown per `CLAUDE.md`).

## Success criteria

- [ ] Every one of the ten commands exits within ~2 seconds with no
      listening port bound, verified by e2e coverage (Task 05.0).
- [ ] No CLI command path constructs `BoundedClickEventQueue`/
      `AnalyticsWorker` or any other background-thread component.
