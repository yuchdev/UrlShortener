# 28 - TUI confirm and read-only tests

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22, 23; Task 06.0 (read-only and safety behaviors)
**Blocks:** 29, 34, 08.0/11
**Estimated size:** S
**Suggested agent:** testing-expert (review: security-auditor)

## Objective

Test ADR 0003 rules 4, 5 and 7 end to end at the TUI level with the mock
client: every mutating command requires confirmation (typed confirmation for
destructive ones), `--yes` skips it, read-only mode renders write actions
disabled with the registry's reason (not hidden) and never sends a mutation,
and secrets never reach the screen.

## Dependencies

- Subtasks 22, 23; 06.0 subtask "read-only mode and mutation safety".
- Catalog: `mutating` flag per descriptor; `link.delete` typed-slug confirmation; the reserved `MIGRATE` confirmation for `migrate`-permission commands (ADR 0003 rule 4; no such command exists in this milestone - the path is tested with a synthetic `CommandInfo` whose permission is `migrate`, matching 06.0/06's `ConfirmPolicy`).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/60_confirm_flow_test.cpp` | create | confirm dialogs |
| `tests/unit/tui/61_read_only_mode_test.cpp` | create | read-only rendering and sends |
| `tests/unit/tui/62_no_secrets_on_screen_test.cpp` | create | secrets scan of frames |
| `tests/unit/tui/golden/*.txt` | create | about 5 goldens |
| `sources.cmake` | modify | Append to `TUI_RENDER_UNIT_SOURCES` (owned by Task 06.0 subtask 01) |

## Classes and functions

| File | Case | Assertion |
|---|---|---|
| 60 | `every_mutating_descriptor_requires_confirm` | iterate descriptors from `commandListFixture()`; for each with `mutating`, open its generic form from the palette, fill minimal valid args, submit: dialog appears and `client.request_texts_for(cmd)` is empty until confirmed |
| 60 | `non_mutating_never_confirms` | read commands send immediately on submit |
| 60 | `delete_requires_typed_slug` | wrong text keeps confirm disabled; exact slug enables; request after confirm only |
| 60 | `cancel_dialog_sends_nothing` | `Esc` and Cancel button |
| 60 | `confirm_is_not_bypassed_by_double_enter` | repeated Enter while dialog open sends exactly one request |
| 60 | `yes_flag_skips_confirm_for_all` | `TuiOptions` with `assume_yes = true` (assigned, C++17 - no designated initializers): immediate send, still exactly one request |
| 60 | `yes_flag_does_not_skip_typed_destructive_confirm` | document the 06.0 decision (ADR 0003 says `--yes` skips dialogs "for scripted/test runs"; if typed confirmation is also skipped, test asserts that and the risk is noted for security-auditor) |
| 61 | `read_only_header_marker` | golden `read_only_frame` |
| 61 | `read_only_write_actions_disabled_with_reason` | goldens `read_only_links_footer` (n/e/d shown disabled), reason text equals the `not_authorized` message the registry returned (from the mock) or the static reason string defined by 06.0 |
| 61 | `read_only_pressing_n_sends_nothing` | `n`, `e`, `d`, `u`, `t` keys: `client.expectNoCalls()` for mutating commands (read commands may be sent) |
| 61 | `read_only_server_denial_still_rendered` | even if the TUI sent a mutation (forced via palette), a `not_authorized` response shows the toast `! not_authorized` with command and id |
| 61 | `read_only_import_screen_disabled` | golden `read_only_import_disabled` |
| 61 | `read_only_palette_marks_write_commands` | write commands shown with permission column and disabled marker |
| 62 | `frames_never_contain_secret_markers` | script mock responses containing fields named `password`, `token`, `salt`, `dsn`, `client_id` (as would only appear through a handler bug) inside `db.rows`/`system.info`: the TUI shows server-provided data verbatim (it does **not** try to be the redaction layer) - this test documents that redaction is enforced in handlers (subtask 18) by asserting that the TUI sources contain no redaction logic and no `db.rows` column filtering (structural), and that when real handlers are used via `InProcessClient` + real registry (reuse subtask 10 fixture) the rendered frames of Tables, System info and Dashboard contain none of the `fake_secret_marker` strings |
| 62 | `tables_screen_only_lists_server_allowlist` | sidebar equals `db.tables` response; TUI does not add tables of its own |

## Implementation steps

1. Reuse `ScreenFixture`; for 62's end-to-end part build the `InProcessClient` over the real registry with `configWithSecrets()` (from subtask 18) - the only TUI test that touches the registry; place it last and name it `..._with_real_registry`.
2. Generate goldens, review against D10 and ADR rule 5 wording ("write screens render disabled with the registry's `not_authorized` reason, not hidden").

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__6[0-2]_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/60_confirm_flow_test.cpp` | `tui__60_confirm_flow_test` | `unit` | 7 |
| `tests/unit/tui/61_read_only_mode_test.cpp` | `tui__61_read_only_mode_test` | `unit` | 7 |
| `tests/unit/tui/62_no_secrets_on_screen_test.cpp` | `tui__62_no_secrets_on_screen_test` | `unit` | 2 (+1 real-registry variant) |

## Constraints

- Iteration over descriptors must be dynamic so new mutating commands are covered automatically.
- Fake secret strings are assembled at runtime (repo secret-scan hook).
- Read-only is a registry-enforced property: the TUI test proves the UI also avoids sending, never that the UI alone is the barrier.

## Success criteria

1. A new descriptor with `mutating=true` without a confirm dialog fails `every_mutating_descriptor_requires_confirm`.
2. Read-only mode sends zero mutating requests across all hotkeys on every screen.
3. No frame in the suite contains the secret marker.

## Out of scope / risks

- Clipboard/export of screen contents: no such feature.
- Risk: `--yes` semantics for typed confirmations may be a security decision for the maintainers (flag in the PR if 06.0 allows skipping).
