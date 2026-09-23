# 01 - Unit tests for new command-service methods

**Parent task:** 05.0 Tests
**State:** ✅ Complete
**Depends on:** [Task 01.0](/docs/roadmap/0003-cli_rest_interfaces/01.0-command-layer-completion/README.md)
**Blocks:** none

## Objective

This subtask is the audit/completion pass over the unit tests Task 01.0's
own subtask 01 already adds (`tests/unit/app/13-16_link_command_service_*.cpp`)
- confirm coverage is complete against every branch in `UpdateLink`,
`DeleteLink`, `SetLinkEnabled`, `RestoreLink`, and `PreviewLink`, using the
same fake-based pattern as the existing `01-12_link_command_service_*.cpp`
tests (`tests/unit/app/app_test_fakes.hpp`).

## Files to modify

- `tests/unit/app/13-16_link_command_service_*.cpp` (added in Task 01.0
  subtask 01) - add any missing branch coverage found here (e.g.
  update-of-nonexistent-slug, restore-of-non-deleted-link, double-disable).
- `tests/unit/app/app_test_fakes.hpp` - extend the fake `ILinkStore` if the
  new methods need fake behavior not already exposed (e.g. a fake that
  tracks `deleted_at`).

## Tests

```bash
ctest --test-dir cmake-build -R "app__|link_command_service" -L unit --output-on-failure
```

## Constraints

- Reuses `app_test_fakes.hpp`'s existing fake infrastructure - no new
  test-double style introduced for these five methods.

## Success criteria

- [ ] Every branch in the five new `LinkCommandService` methods has at
      least one unit test.
- [ ] All app-layer unit tests pass under `ctest -L unit`.
