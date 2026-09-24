# 01 - Success envelope reuse

**Parent task:** 04.0 CLI output and error contract
**State:** ✅ Complete
**Depends on:** none (Task 03.0 subtask 01 must exist as the call site)
**Blocks:** 02

## Objective

Print exactly one JSON object to stdout on success, for every command,
built from the exact same serializer the REST handler for that command
uses - `app::serializeLinkViewJson` for create/get/update/delete/enable/
disable/restore/preview, `app::serializeLinkStatsJson` for stats.

## Files to modify

- `src/cli/link_command_dispatch.cpp` - on `Result::ok()`, call the
  matching serializer and write its output plus a trailing newline to
  stdout.

## Tests

- `tests/integration/cli/` (Task 05.0 subtask 03) is the real evidence
  (spawn binary, parse stdout as JSON); this subtask's own unit-level check
  is that `link_command_dispatch.cpp` calls `serializeLinkViewJson`/
  `serializeLinkStatsJson` directly rather than re-implementing
  serialization - a code-review-level constraint, not something a unit
  test alone proves.

## Constraints

- No second JSON serializer for `LinkView`/`LinkStatsView` is written for
  the CLI path - if the existing serializers are missing a field the CLI
  needs, extend them (benefiting REST too), don't fork them.
- Exactly one line to stdout per successful invocation, no additional log
  lines mixed in (logging, if any, goes to stderr or is suppressed for CLI
  mode).

## Success criteria

- [ ] Every successful `link <verb>` invocation prints one JSON line to
      stdout using the shared serializer for that result type.
- [ ] No duplicate serialization logic exists between the REST and CLI
      paths.
