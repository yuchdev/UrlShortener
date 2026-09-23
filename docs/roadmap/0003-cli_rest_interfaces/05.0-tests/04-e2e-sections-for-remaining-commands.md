# 04 - E2E sections for remaining commands

**Parent task:** 05.0 Tests
**State:** ✅ Complete
**Depends on:** [Task 03.0](/docs/roadmap/0003-cli_rest_interfaces/03.0-cli-dispatch-and-lifecycle/README.md)
**Blocks:** none

## Objective

Extend the shell-based e2e pattern
(`tests/e2e/scripts/sections/11_cli_link_create.sh`,
`12_cli_link_get.sh`, `13_cli_no_server_socket.sh`) with new numbered
sections for the six remaining commands, each including the same
"no listening socket" assertion `13_cli_no_server_socket.sh` already makes
for `create`.

## Files to add

- `tests/e2e/scripts/sections/14_cli_link_update.sh`
- `tests/e2e/scripts/sections/15_cli_link_delete.sh`
- `tests/e2e/scripts/sections/16_cli_link_enable_disable.sh`
- `tests/e2e/scripts/sections/17_cli_link_restore.sh`
- `tests/e2e/scripts/sections/18_cli_link_preview.sh`

## Files to modify

- `tests/e2e/scripts/run_all_sections.sh` - register the new sections.
- `CMakeLists.txt` - register corresponding `add_test` entries with the
  `e2e` label, matching the existing pattern for sections 11-13.

## Tests

```bash
ctest --test-dir cmake-build -L e2e --output-on-failure
```

## Constraints

- POSIX-shell/`bash`, `python3`, and Linux-runtime-facility dependent
  (`/proc`, `os.kill`), per `CLAUDE.md` - these sections register only on
  non-Windows hosts, consistent with the existing `e2e` label behavior.
- Each new section explicitly checks no listening port is bound after the
  command exits, per C4.

## Success criteria

- [ ] Five new e2e sections exist, covering all six remaining commands
      (enable/disable share one section).
- [ ] `ctest -L e2e` passes with all new sections registered and green.
