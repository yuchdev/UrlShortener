# 03 - Integration tests for remaining commands

**Parent task:** 05.0 Tests
**State:** ⬜ Not started
**Depends on:** [Task 03.0](/docs/roadmap/0003-cli_rest_interfaces/03.0-cli-dispatch-and-lifecycle/README.md), [Task 04.0](/docs/roadmap/0003-cli_rest_interfaces/04.0-cli-output-and-error-contract/README.md)
**Blocks:** 04

## Objective

First, confirm `tests/integration/cli/cli_integration_common.py`'s
`_probe_cli_support` no longer self-skips the suite (it currently detects
that the binary doesn't exit promptly and skips everything) - this is the
concrete proof that Tasks 03.0/04.0 actually work end-to-end, not just at
the unit level. Then extend the same integration pattern used by
`01-10_*.py` (spawn the binary, assert stdout JSON, verify state via a
follow-up CLI invocation where the storage-scoping question from Task 03.0
subtask 02 allows it) to the six new commands.

## Files to modify

- `tests/integration/cli/cli_integration_common.py` - remove or adjust
  `_probe_cli_support`'s skip condition once it is no longer needed;
  confirm via a control run that the probe now reports CLI support present.

## Files to add

- `tests/integration/cli/11-20_link_{update,delete,enable,disable,restore,preview}_*.py`
  following the existing numbering/naming convention (`01_link_create_returns_json.py`
  style).

## Tests

```bash
ctest --test-dir cmake-build -L integration --output-on-failure
```

## Constraints

- If Task 03.0 subtask 02 concluded that separate CLI process invocations
  do **not** share `linkRepository()` state, these tests must account for
  that (e.g. test `update`/`delete`/`enable`/`disable`/`restore`/`preview`
  against a link created via the REST API within the same test process/
  server fixture, not via a separate CLI `create` invocation) - do not
  write a test that silently assumes cross-invocation persistence without
  first confirming it holds.

## Success criteria

- [ ] `_probe_cli_support` reports CLI mode is supported (no longer
      self-skips).
- [ ] All six new commands have integration coverage matching the depth of
      the existing `create`/`get`/`stats` suite (happy path + at least one
      error path each).
- [ ] `ctest -L integration` passes with the CLI suite actually executing.
