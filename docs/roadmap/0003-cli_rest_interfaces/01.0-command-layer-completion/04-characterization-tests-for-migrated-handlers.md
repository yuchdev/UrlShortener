# 04 - Characterization tests for migrated handlers

**Parent task:** 01.0 Command layer completion
**State:** ⬜ Not started
**Depends on:** 02, 03
**Blocks:** none

## Objective

Provide the non-regression evidence Task 01.0 promises in `plan.md`'s C3:
prove, with tests written against (or confirmed to already exist against)
the pre-migration handlers, that the migration in subtasks 02/03 changed
nothing observable over HTTP.

This subtask is deliberately about *evidence*, distinct from subtasks
02/03's *implementation* - if subtasks 02/03 already leave
`tests/unit/http/10_link_handlers.cpp` fully green with no case additions
needed, this subtask's job is to confirm that coverage is actually complete
(every field, every error path) and record it in `status.md`, not to
duplicate work.

## Files to modify

- `tests/unit/http/10_link_handlers.cpp` - fill any gaps found (e.g. a
  campaign-clearing PATCH case, a disable-then-enable round trip, a preview
  of a soft-deleted link) discovered while auditing coverage against the
  pre-migration handler bodies.

## Tests

Run, before and after subtasks 02/03 land, and compare output byte-for-byte
where practical:

```bash
ctest --test-dir cmake-build -R "^10_link_handlers$" --output-on-failure -L unit
ctest --test-dir cmake-build -L "unit|contract|integration|e2e" --output-on-failure
```

## Constraints

- No test in this subtask may be weakened (fewer assertions, looser
  matching) to make the migration pass - if a real behavior change is
  discovered, stop and resolve it in subtasks 02/03 first.

## Success criteria

- [ ] `tests/unit/http/10_link_handlers.cpp` demonstrably covers every
      field/branch touched by the migrated handlers.
- [ ] Full test suite (`unit|contract|integration|e2e`) passes after
      migration with no case removed or loosened relative to before.
- [ ] `status.md` records the before/after test counts as evidence, per the
      convention used in Milestone 0001's `status.md`.
