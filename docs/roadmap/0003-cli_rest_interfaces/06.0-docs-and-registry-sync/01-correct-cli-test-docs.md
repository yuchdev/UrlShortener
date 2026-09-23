# 01 - Correct CLI test docs

**Parent task:** 06.0 Docs and registry sync
**State:** ✅ Complete
**Depends on:** [Task 05.0](/docs/roadmap/0003-cli_rest_interfaces/05.0-tests/README.md)
**Blocks:** none

## Objective

Rewrite `docs/testing/cli_link_commands.md` and
`docs/testing/testplans/cli.md` so they describe the CLI as actually built
by Tasks 01.0-05.0, not the pre-existing (incorrect) design assumptions -
principally, removing every reference to `uri.txt` persistence (per
`plan.md`'s Background correction) and adding manual QA / test-plan
coverage for the six new commands.

## Files to modify

- `docs/testing/cli_link_commands.md` - replace the "Prerequisites"
  checklist (now satisfied) with the actual implemented behavior; replace
  `uri.txt`-based expectations with whatever Task 03.0 subtask 02 concluded
  about storage scoping across CLI invocations; add test cases for
  `update`/`delete`/`enable`/`disable`/`restore`/`preview` mirroring the
  existing create/get/stats case structure.
- `docs/testing/testplans/cli.md` - update "Commands under test" to list
  all ten commands; update "Required coverage" and "E2E analogs" to
  reference the files added in Task 05.0.

## Tests

- `/link-check` skill (or manual review) - confirm no dangling references
  remain to removed/renamed sections.

## Constraints

- Every claim in the corrected docs must be verifiable against the actual
  shipped code/tests from Tasks 01.0-05.0, not aspirational - this document
  previously drifted from reality once before (the `uri.txt` assumption);
  this subtask exists specifically to prevent that recurring.

## Success criteria

- [ ] No remaining reference to `uri.txt` in either file.
- [ ] All ten commands have manual QA test cases and a test-plan entry.
- [ ] Storage-scoping behavior across CLI invocations (Task 03.0 subtask
      02's finding) is stated accurately.
