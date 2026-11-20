# 25 - TUI snapshots: forms, jobs, palette, dialogs (consumed by 06.0)

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 23; Task 06.0 (form model, palette, import/export and jobs screens, dialogs)
**Blocks:** 29, 34, 08.0/10, 08.0/11
**Estimated size:** XS (traceability only; the goldens shipped in 06.0)
**Suggested agent:** testing-expert

> **Amended 2026-11-20 (as-built reconciliation).** 06.0 shipped the D5, D7, D8,
> D9 and D10 goldens and their render tests. The class is `FormModel` (not
> `FormBuilder`); the "Equivalent" footer lives in `equivalent_cmd.hpp`; import
> submit is `Ctrl-S` and the raw-JSON toggle is `Ctrl-R` (06.0 reconciliation);
> there is no `ScreenFixture`, no `client.requestsFor`, and no fake-clock advance
> API. This subtask is a **consumed-coverage record** with no new test file. The
> only behavior worth a dedicated 07.0 gap check - a generic form for a
> descriptor unknown at compile time (ADR 0003 rule 2) - is owned by subtask 26
> (FormModel property tests) and the shipped `tui__11_generic_command_flow_test`.

## Coverage already delivered (06.0)

| Diagram | Delivered by (consumed) | Goldens / request fixtures |
|---|---|---|
| D5 new/edit form, enum dropdown, scroll, read-only, validation errors | `tui__08_form_screen_test`, `tui__08_form_widgets_render_test`, `tui__07_form_model_request_test` | `d05_new_link_80x24`, `d05_edit_link_80x24`, `d05_new_link_errors_80x24`, `d05_generic_enum_dropdown_80x24`, `d05_form_scroll_80x12`, `d05_new_link_readonly_80x24`, `07_request_link_create.json`, `07_request_link_update_clear.json`, `07_request_link_list_all.json` |
| D7 import/export (paste, file, too-large, export, read-only) | `tui__15_import_export_screen_test` | `d07_import_80x24`, `d07_import_file_80x24`, `d07_import_toolarge_80x24`, `d07_export_80x24`, `d07_import_readonly_80x24` |
| D8 jobs (mixed states, empty, mono, job detail) + poll guard | `tui__16_jobs_screen_test`, `tui__16_poll_ticker_test` | `d08_jobs_80x24`, `d08_jobs_empty_80x24`, `d08_jobs_mono_80x24`, `d08_job_detail_failed_80x24` |
| D9 palette + generic command flow (zero-TUI-code form) | `tui__11_palette_render_test`, `tui__11_generic_command_flow_test` | `d09_palette_empty_80x24`, `d09_palette_link_prefix_80x24`, `d09_palette_readonly_80x24`, `d09_result_view_80x24` |
| D10 confirm (typed-slug + y/n) and toasts | `tui__06_dialog_render_test`, `tui__06_confirm_test` | `d10_confirm_delete_80x24`, `d10_confirm_yesno_80x24`, `d10_toast_error_80x24`, `d10_toast_details_80x24` |

The "Equivalent" footer is proven by `tui__06_equivalent_cmd_test`
(`equivalent_cmd.hpp`); import submit is `Ctrl-S` and the raw-JSON toggle is
`Ctrl-R` (06.0 reconciliation), not the `Ctrl-J`/button wording this spec
originally assumed.

## Dependencies

- Subtasks 22, 23; ADR 0003 D5, D7-D10; the shipped goldens above.

## Files affected

No new test file. The 06.0 goldens and tests satisfy this subtask. If 29 finds a
missing D5/D7-D10 state, add one golden under a free `tests/unit/tui/24-29` stem
reusing the shipped harness (box glyphs, `d0N_*` naming). The exact-envelope
request assertions use the shipped `*_request_*.json` fixtures plus
`request_texts_for` from subtask 22's `tui_test_support.hpp`.

## Implementation steps

1. During 29, diff the shipped D5/D7-D10 goldens against ADR 0003; record each as `auto`/`both`.
2. Only if a state is unproven, add a single golden test reusing the shipped harness; drive the screen component directly (no `ScreenFixture`, no fake clock).

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__(06|07|08|11|15|16)_" --output-on-failure
```

## Tests

None new unless 29 finds a gap; the proving targets are `tui__06`, `tui__07`,
`tui__08`, `tui__11`, `tui__15`, `tui__16` (see the coverage table).

## Constraints

- Reuse the shipped harness only; no `ScreenFixture`, no `client.requestsFor`, no fake clock.
- Request assertions compare exact serialized text (the shipped `*_request_*.json` fixtures already do).

## Success criteria

1. Every D5/D7-D10 state in ADR 0003 maps to a shipped golden (recorded in 29).
2. The zero-TUI-code generic form is proven by `tui__11_generic_command_flow_test` and subtask 26.

## Out of scope / risks

- This spec is a candidate to fold into 29 (traceability); see the amendment report.
