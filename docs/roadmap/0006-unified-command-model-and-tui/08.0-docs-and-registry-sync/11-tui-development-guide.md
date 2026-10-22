# 11 - TUI development guide

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 10; Task 07.0 subtasks 22-28
**Blocks:** 13, 17
**Estimated size:** S
**Suggested agent:** docs-writer

## Objective

Create `docs/tui/development.md`: how contributors add a screen or a command
form, keep the TUI free of business logic, and test it with the mock client and
golden snapshots - the documentation form of ADR 0003 rules 1-7.

## Dependencies

- ADR 0003; 06.0 sources under `src/tui/`; 07.0 harness (`MockCommandClient`, `renderToText`, `Golden`, `ScreenFixture`).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/tui/development.md` | create | the guide |

## Classes and functions (headings to write)

1. `## Architecture in one page` - diagram from ADR 0003 Decision 2; directory map of `src/tui/` as built.
2. `## Rules` - the seven rules, each with the test that enforces it (rule 1 -> `tui__02_include_boundary_test` and `commands__79_no_http_server_in_local_modes_test` structural case; rule 4 -> `tui__60_confirm_flow_test`; rule 5 -> `tui__61_read_only_mode_test`; rule 7 -> `tui__62_no_secrets_on_screen_test`).
3. `## Adding a command to the TUI` - normally nothing: register the descriptor (see catalog section 6); palette and generic form appear automatically (proof test `tui__33_...` case `palette_new_command_zero_tui_code`).
4. `## Adding a bespoke screen` - steps: screen class, route in the router/state-machine table, hotkey table entry, help overlay text, mock-driven snapshot test, golden review, ADR 0003 diagram update when layout is normative.
5. `## FormBuilder and ArgSpec types` - supported types and widgets table; how to extend when a new `ArgSpec` type is added (property test will fail first).
6. `## Threading` - sync commands on worker, response posting, never block the UI thread; job polling timer.
7. `## Testing` - harness API (06.0/01 helpers extended by 07.0/23, one mock from 06.0/02 extended by 07.0/22, the two TUI test lists of 06.0/01), `UPDATE_GOLDEN` workflow, JSON via `json_view` over the commands scanner, determinism rules (mono, 80x24, fake clock, ids), how to debug a failing golden diff, running with `FORM_PROP_SEED`.
8. `## Dependencies` - FTXUI version pin, how to upgrade (regenerate goldens, run manual checklist), `URLSHORTENER_ENABLE_TUI=OFF` build.
9. `## Checklist for a TUI PR` - 8 items.

## Implementation steps

1. Read `src/tui/` tree and tests as merged; 2. verify every referenced test target with `ctest -N`; 3. keep under ~200 lines; 4. `/link-check docs/tui`.

## Commands

```bash
ctest --test-dir cmake-build -N -R "^tui__"
```

## Tests

None executable.

## Constraints

- Contributor-facing; no operator content (that is subtask 10).
- Every test name cited must exist.

## Success criteria

1. All nine headings exist; every cited target appears in `ctest -N`.
2. A contributor can add a bespoke screen using only this guide (reviewed by someone who did not write 06.0).

## Out of scope / risks

- API reference for FTXUI. Risk: file names change before merge; run last in the group.
