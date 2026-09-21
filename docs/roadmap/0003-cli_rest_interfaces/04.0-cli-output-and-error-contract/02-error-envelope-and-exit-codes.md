# 02 - Error envelope and exit codes

**Parent task:** 04.0 CLI output and error contract
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** none

## Objective

Define one `AppErrorCode -> (exit code, stderr message)` mapping, used by
every `link <verb>` command, as the CLI-side counterpart to
`link_handlers.cpp`'s existing `statusForAppError`/`codeForAppError`
(which map `AppErrorCode` to HTTP status/error-code strings).

## Files to add

- `include/url_shortener/cli/link_command_dispatch.hpp` /
  `src/cli/link_command_dispatch.cpp` - add
  `int ExitCodeForAppError(app::AppErrorCode code)` and a stderr message
  formatter, mirroring the switch structure of `statusForAppError`/
  `codeForAppError` (same `AppErrorCode` enum, different output shape - not
  a HTTP status).

Suggested mapping (finalize during implementation, not prescriptive):

```text
AppErrorCode::none               -> 0 (not an error path)
AppErrorCode::not_found          -> 1
AppErrorCode::invalid_url
AppErrorCode::invalid_slug
AppErrorCode::invalid_field      -> 2 (bad input)
AppErrorCode::reserved_slug
AppErrorCode::slug_conflict      -> 3 (conflict)
AppErrorCode::storage_failure
AppErrorCode::internal           -> 4 (server/internal failure)
```

## Tests

- `tests/integration/cli/` (Task 05.0 subtask 03) - for each error case
  already covered by the manual QA checklist
  (`docs/testing/cli_link_commands.md`: not-found, invalid URL/SSRF guard,
  reserved slug, duplicate slug, invalid stats window), assert the process
  exit code matches this mapping and stderr contains the error detail.

## Constraints

- stdout is reserved for the success JSON envelope only (Task 04.0 subtask
  01) - error text never goes to stdout, so a script can safely do
  `result=$(url_shortener link get --slug x)` and know a non-empty stdout
  means success.
- The mapping is defined once, in one function, and every `link <verb>`
  command's dispatch code calls it - no per-command ad hoc exit code.

## Success criteria

- [ ] `ExitCodeForAppError` exists and is used by every command's error
      path.
- [ ] Every error case in `docs/testing/cli_link_commands.md`'s test cases
      5-9 produces the mapped exit code and a stderr message.
- [ ] stdout is empty on any non-zero exit.
