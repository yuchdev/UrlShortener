# 25 - TUI snapshots: forms, jobs, palette, dialogs

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 23; Task 06.0 (form builder, palette, import/export and jobs screens, dialogs)
**Blocks:** 29, 34, 08.0/10, 08.0/11
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Golden-text and exact-envelope tests for the interactive screens: D5 new/edit
form (with the "Equivalent" footer), D7 import/export, D8 jobs, D9 command
palette, D10 confirm and error toast.

## Dependencies

- Subtasks 22, 23, `fixtures.hpp` from 24 (or its own copy if 24 is not merged).
- ADR 0003 D5, D7-D10; catalog `link.create`/`link.update`/`link.import` ArgSpec.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/30_snapshot_link_form_test.cpp` | create | D5 create and edit |
| `tests/unit/tui/31_snapshot_import_export_test.cpp` | create | D7 |
| `tests/unit/tui/32_snapshot_jobs_test.cpp` | create | D8 + job detail |
| `tests/unit/tui/33_snapshot_palette_and_dialogs_test.cpp` | create | D9, D10 |
| `tests/unit/tui/golden/*.txt` | create | about 24 goldens |
| `sources.cmake` | modify | Append four tests to `TUI_RENDER_UNIT_SOURCES` (owned by Task 06.0 subtask 01) |

## Classes and functions

Cases (golden name -> scenario; "req" = exact request envelope asserted with `client.requestsFor`):

| File | Golden / case | Scenario |
|---|---|---|
| 30 | `form_link_create_blank` | all `link.create` ArgSpec fields in catalog order, required `*`, enum radio for `redirect_type`, bool checkbox for `enabled`, footer "Equivalent: url_shortener cmd link.create --json '{...}'" |
| 30 | `form_link_create_filled` | typed values; footer JSON updates live; req on Create: exact args (tags split by comma, metadata `k=v`, campaign object only with filled subfields) |
| 30 | `form_link_create_validation_error` | server returns `invalid_url` -> inline field error, form stays open, no duplicate send |
| 30 | `form_link_create_slug_conflict_toast` | D10 right toast |
| 30 | `form_link_edit_prefilled` | from `link.get` result; `link.update` req carries **only changed fields**; cleared expires -> `"expires_at":null` |
| 30 | `form_cancel_sends_nothing` | `client.expectNoCalls()` after Cancel |
| 31 | `import_paste_csv` | D7 with pasted rows, format radio, conflict radio, dry-run checkbox; footer equivalent line |
| 31 | `import_submit_request` | req `link.import` exact args (`format`, `data`, `on_conflict`, `options.dry_run`); after job accepted, navigates to Jobs |
| 31 | `import_path_source_local_only` | file path input present (local client); `path` sent, `data` absent |
| 31 | `import_overwrite_option_visible_local` | `overwrite` offered in TUI (not REST, catalog) |
| 31 | `export_form` | `link.export` fields; result job navigation |
| 32 | `jobs_list_mixed_states` | D8 rows: running with progress bar, succeeded, failed with code, cancelled |
| 32 | `jobs_autorefresh_polls` | advance fake clock 1 s: one `job.list` (or `job.get`) per tick, no more (poll storm guard) |
| 32 | `jobs_cancel_flow` | `c` -> confirm -> `job.cancel` req exact |
| 32 | `job_detail_full_error_envelope` | Enter shows the raw error envelope |
| 32 | `jobs_empty` | no jobs |
| 33 | `palette_open_all_commands` | D9 list built from the `command.list` fixture (real descriptors), permission column, `(confirm)` marker on mutating |
| 33 | `palette_filter_typed` | `link.` filters; `db.r` -> `db.rows` only |
| 33 | `palette_new_command_zero_tui_code` | add a synthetic descriptor to the `command.list` fixture at runtime (string arg + enum arg): palette lists it and the generic form renders with correct widgets - the executable proof of ADR 0003 rule 2 |
| 33 | `palette_select_opens_generic_form` | req on submit equals envelope built from form values |
| 33 | `confirm_delete_typed_slug` | D10 left dialog; wrong slug keeps Delete disabled; right slug enables; req `link.delete` only after confirm |
| 33 | `confirm_generic_mutating` | y/n confirm for other mutating commands; `--yes` option skips it (req sent immediately) |
| 33 | `error_toast_details_raw_envelope` | `d` shows raw envelope text exactly as returned |

## Implementation steps

1. Reuse `ScreenFixture`; for the synthetic-descriptor case build the fixture JSON by appending to `commandListFixture()` output through a tiny helper (string insertion before the closing `]`), not by modifying the real registry.
2. Fake clock advance API from 06.0 (**UNVERIFIED**; raise as a requirement if absent).
3. Generate goldens, review against D5/D7-D10, commit.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__3[0-3]_" --output-on-failure
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/30_snapshot_link_form_test.cpp` | `tui__30_snapshot_link_form_test` | `unit` | 6 |
| `tests/unit/tui/31_snapshot_import_export_test.cpp` | `tui__31_snapshot_import_export_test` | `unit` | 5 |
| `tests/unit/tui/32_snapshot_jobs_test.cpp` | `tui__32_snapshot_jobs_test` | `unit` | 5 |
| `tests/unit/tui/33_snapshot_palette_and_dialogs_test.cpp` | `tui__33_snapshot_palette_and_dialogs_test` | `unit` | 7 |

## Constraints

- Requests compared as exact serialized text (byte compare), per the 07.0 acceptance wording.
- Typed-confirmation inputs use only fake data; no real clipboard/IO.
- Mono theme, 80x24, fake clock.

## Success criteria

1. Goldens exist for D5, D7, D8, D9, D10 and the listed variants.
2. `palette_new_command_zero_tui_code` passes (generic form for a descriptor unknown at compile time).
3. Every mutating screen action is proven to send nothing before confirmation and exactly one request after.

## Out of scope / risks

- Key-by-key form field editing semantics are in subtask 26.
- Risk: FTXUI `Input` widgets' internal cursor rendering may vary; harness strips the cursor glyph (document in 23 if needed).
