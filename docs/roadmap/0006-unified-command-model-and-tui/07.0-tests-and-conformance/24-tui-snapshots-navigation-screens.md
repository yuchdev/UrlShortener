# 24 - TUI snapshots: navigation screens (consumed by 06.0)

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 23; Task 06.0 subtasks (app shell, links/tables/detail screens, dashboard)
**Blocks:** 29, 34, 08.0/10, 08.0/11
**Estimated size:** XS (traceability only; the goldens shipped in 06.0)
**Suggested agent:** testing-expert

> **Amended 2026-11-20 (as-built reconciliation).** 06.0 already shipped the D1,
> D2, D3, D4 and D6 goldens **and** their render tests, rendering each screen
> directly (no full `App`). The originally-specced stems `20_`/`21_`/`22_`
> **collide** with the shipped `tui__20_app_navigation_test` / `tui__21_settings_*`;
> `ScreenFixture`, `App::currentScreen()` and the fake clock do **not** exist
> (`App::run()` starts the loop; screens are driven directly via `send_events`).
> This subtask is therefore a **consumed-coverage record** with no new test file.
> If a specific navigation golden is found missing during 29's traceability pass,
> add it under a free stem in `tests/unit/tui/24-29` reusing the shipped harness.
> See the report note: this spec is a candidate to fold into 29.

## Coverage already delivered (06.0)

| Diagram | Delivered by (consumed) | Goldens |
|---|---|---|
| D1 frame (header, tabs, footer, memory banner, read-only, narrow) | `tui__04_frame_render_test` (base, memory banner, narrow); `tui__05_theme_render_test` (mono, light); `tui__18_readonly_render_test` (read-only) | `d01_frame_80x24`, `d01_frame_mono_80x24`, `d01_frame_light_80x24`, `d01_frame_memory_banner_80x24`, `d01_frame_readonly_80x24`, `d01_frame_narrow_60x20` |
| D2 dashboard (populated, empty/partial, memory, stats, mono) | `tui__17_dashboard_screen_test` | `d02_dashboard_80x24`, `d02_dashboard_partial_failure_80x24`, `d02_dashboard_memory_80x24`, `d02_dashboard_stats_summary_80x24`, `d02_dashboard_mono_80x24` |
| D3 links table + request assertions | `tui__12_links_screen_test`, `tui__10_table_view_render_test` (`d03_table_20x`); `tui__12_links_query_test` (`12_request_link_list_*.json`) | `d03_links_80x24`, `d03_links_error_80x24`, `d03_links_empty_80x24`, `d03_links_filter_80x24`, `d03_links_readonly_80x24`, `d03_table_20x_80x24`, `12_request_link_list_all.json`, `12_request_link_list_active.json` |
| D4 link detail/stats | `tui__13_link_detail_screen_test` | `d04_detail_80x24`, `d04_detail_stats_error_80x24`, `d04_detail_deleted_80x24`, `d04_detail_no_stats_80x24`, `d04_detail_mono_80x24` |
| D6 tables browser + inline row JSON | `tui__14_tables_screen_test` | `d06_tables_80x24`, `d06_tables_events_80x24`, `d06_tables_row_json_80x24`, `d06_tables_mono_80x24`, `d06_tables_empty_80x24`, `d06_tables_error_80x24` |

Row JSON is hosted **inline** in `tables_screen` (06.0 spec reconciliation:
`row_json_screen.cpp` superseded by inline hosting), so `d06_tables_row_json`
covers the originally-separate "row JSON view".

## Objective

Record that the navigation-style snapshots (D1 frame, D2 dashboard, D3 links,
D4 detail/stats, D6 tables + inline row JSON) are proven by the shipped
`tui__04`/`10`/`12`/`13`/`14`/`17` tests above. No new goldens unless 29 finds a
gap.

## Dependencies

- Subtasks 22, 23.
- ADR 0003 diagrams D1-D4, D6 as the review reference: goldens are not required to be byte-equal to the ADR art (borders/spacing may differ), but **content, column set, ordering, hotkey footer and disabled/empty/error states must match** the diagrams; deviations amend the ADR (06.0 rule).

## Files affected

No new test file by default. The 06.0 goldens and tests above satisfy this
subtask. If 29's traceability pass finds a specific missing navigation golden,
add one file under a free `tests/unit/tui/24-29` stem (checked unused;
`20_`/`21_`/`22_` are taken by 06.0), reusing the shipped `render_to_text` /
`assert_matches_golden` / `send_events` / `InlinePoster::run_pending`. Goldens
keep box glyphs and get the shipped `d0N_*_80x24` naming.

## Implementation steps

1. During 29, diff the shipped goldens above against ADR 0003 D1-D4/D6; record each as `auto` in the traceability table.
2. Only if a diagram state is unproven, add a single golden test reusing the shipped harness (no `ScreenFixture`, no `App::currentScreen`, no fake clock - drive the screen component directly with `send_events`).

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__(04|05|10|12|13|14|17|18)_" --output-on-failure
```

## Tests

None new unless 29 finds a gap; the proving targets are `tui__04`, `tui__10`,
`tui__12`, `tui__13`, `tui__14`, `tui__17` (see the coverage table).

## Constraints

- Reuse the shipped harness only; do not introduce a second renderer, golden dir, `ScreenFixture`, or fake clock.
- Any added golden keeps the shipped box-glyph, right-trimmed, 80x24 format.

## Success criteria

1. Every D1/D2/D3/D4/D6 state in ADR 0003 maps to a shipped golden (recorded in 29).
2. No new `tui__2[0-2]_` stem is introduced (they collide with 06.0).

## Out of scope / risks

- This spec is a candidate to fold entirely into 29 (traceability); see the amendment report.
