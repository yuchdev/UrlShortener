# 02 - CLI command reference

**Parent task:** 06.0 Docs and registry sync
**State:** ✅ Complete
**Depends on:** [Task 05.0](/docs/roadmap/0003-cli_rest_interfaces/05.0-tests/README.md)
**Blocks:** none

## Objective

Add a durable reference for the new CLI surface, in the same spirit as
`docs/api/README.md` for REST - each of the ten commands cross-linked to
its `operation_id` from that file, so the two interfaces stay visibly
paired as they evolve.

## Files to add

- `docs/cli/README.md` - one row per command: CLI invocation, corresponding
  REST `operation_id`, flags, exit codes (from Task 04.0 subtask 02's
  mapping), example invocation and output.

## Files to modify

- `README.md` (repo root) - add a short "CLI mode" section under the
  existing "API examples" section, pointing at `docs/cli/README.md`, with
  one or two example invocations (mirroring the style of the existing curl
  examples).
- `docs/roadmap/README.md` - none required beyond what was already updated
  when this milestone's plan/status were added.

## Tests

None (documentation-only); validated by `/link-check` and manual review
against the actually-shipped flag names from Task 02.0.

## Constraints

- Field-level documentation (what each JSON field in the response means)
  stays in `docs/api/README.md`/the route registry - this reference only
  adds the CLI-specific invocation shape and links out rather than
  duplicating REST field docs.

## Success criteria

- [ ] `docs/cli/README.md` exists with all ten commands, each linked to its
      `operation_id`.
- [ ] Root `README.md` links to it from a new CLI section.
- [ ] No REST field documentation is duplicated rather than linked.
