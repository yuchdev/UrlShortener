# 24 - TUI snapshots: navigation screens

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 23; Task 06.0 subtasks (app shell, links/tables/detail screens, dashboard)
**Blocks:** 29, 34, 08.0/10, 08.0/11
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Golden-text tests for the read-only, navigation-style screens of ADR 0003:
D1 frame, D2 dashboard, D3 links table, D4 link detail/stats/preview, D6 tables
browser (and the row JSON view), each rendered from scripted mock responses.

## Dependencies

- Subtasks 22, 23.
- ADR 0003 diagrams D1-D4, D6 as the review reference: goldens are not required to be byte-equal to the ADR art (borders/spacing may differ), but **content, column set, ordering, hotkey footer and disabled/empty/error states must match** the diagrams; deviations amend the ADR (06.0 rule).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/20_snapshot_frame_and_dashboard_test.cpp` | create | D1, D2 |
| `tests/unit/tui/21_snapshot_links_and_detail_test.cpp` | create | D3, D4 |
| `tests/unit/tui/22_snapshot_tables_browser_test.cpp` | create | D6 + row JSON |
| `tests/unit/tui/golden/*.txt` | create | about 22 golden files (names below) |
| `tests/unit/tui/fixtures.hpp` | create | shared scripted responses (`seedLinksPage()`, `seedStats()`, `seedTables()`, fixed ids/timestamps) |
| `sources.cmake` | modify | Append three tests to `TUI_RENDER_UNIT_SOURCES` (owned by Task 06.0 subtask 01) |

Note: stems `20_`-`22_` here; subtask 22 uses `23_`, 23 `24_`, 25 `30_`-`33_`, 26 `40_`, 27 `50_`, 28 `60_` so numbering never collides inside `tests/unit/tui/` (06.0 owns `01`-`19`; targets are `tui__<stem>`). Golden file names get the diagram prefix required by 06.0 README K4 (`d01_frame_80x24_dashboard.txt`, `d03_links_page1_status_all.txt`, ...).

## Classes and functions

Fixture: `struct ScreenFixture { MockCommandClient client; ftxui::Component app; ScreenFixture(TuiOptions opts = {}); std::string frame(); }`. Fixed data in `fixtures.hpp` (ids `01HZX00000000000000000K3`..., timestamps at `2026-09-02T10:14:00Z`, clock `2026-10-19T10:44:02Z`).

| File | Golden name | Scenario |
|---|---|---|
| 20 | `frame_80x24_dashboard` | D1 frame: header shows backend/user, tab bar, footer hotkeys |
| 20 | `frame_memory_store_banner` | `system.info` `persistent:false` -> "memory store - data is lost on exit" banner |
| 20 | `frame_read_only_header` | `read_only:true` shows read-only marker in header |
| 20 | `frame_too_small_60x20` | below 80x24: defined "terminal too small" message (06.0 decision; case removed if 06.0 has none) |
| 20 | `dashboard_populated` | D2 numbers from `link.list` totals per status + jobs; top slugs/recent changes sections render only data available from commands (no command feeds "Redirects (24h)" or "p50": golden asserts "n/a", per the ADR 0003 D2 "Data sources" note and 06.0/17) |
| 20 | `dashboard_empty` | zero links, no jobs |
| 20 | `dashboard_error_toast` | `link.list` returns `storage_failure` -> toast with code, no crash |
| 21 | `links_page1_status_all` | D3 table, selected row marker, status column, paging text `page 1/61` computed from `total`/`limit` |
| 21 | `links_filter_applied` | filter bar text -> request carries `q` (asserted via `client.request_texts_for("link.list")`) |
| 21 | `links_empty` | "no links match" state |
| 21 | `links_long_target_truncated` | ellipsis, column widths stable |
| 21 | `links_deleted_row_style_marker` | deleted/disabled shown textually in mono theme |
| 21 | `links_loading_state` | before response arrives (latency set) shows loading indicator |
| 21 | `link_detail_full` | D4 all fields, tags, campaign, metadata |
| 21 | `link_detail_stats_week` | stats bar rows from `link.stats` buckets |
| 21 | `link_detail_not_found_error` | `link.get` -> `not_found` toast and return to list |
| 22 | `tables_sidebar_links_selected` | D6 sidebar rows with counts from `db.tables` |
| 22 | `tables_rows_links` | rows grid with columns from `db.rows` `columns` (generic) |
| 22 | `tables_rows_click_events_readonly` | insert hint absent for non-writable tables |
| 22 | `tables_horizontal_scroll_offset` | wide table scrolled by N columns (ADR negative consequence: TableView implements horizontal scrolling) |
| 22 | `tables_row_json_view` | Enter shows exact `result` item JSON |
| 22 | `tables_empty_table` | zero rows |

Each case asserts both `assert_matches_golden(frame, name)` **and** the requests sent (e.g. page 2 -> `offset` 20; sort key -> `sort`/`order`), using exact string equality against expected envelope text (`{"v":1,"id":"...","command":"link.list","args":{...}}`; ids: the TUI's id generator must be deterministic in tests - 06.0 requirement, **UNVERIFIED**).

## Implementation steps

1. Wait for the screen factory entry points from 06.0; write `fixtures.hpp` first and review it against `docs/commands/README.md` result shapes (never invent fields).
2. For each case: script responses, press keys with `press_keys`, `drain`, `render_to_text`, `assert_matches_golden` (helpers of 06.0/01 extended by 23).
3. Generate goldens once with `UPDATE_GOLDEN=1`, then **read every golden against the ADR diagram** before committing (review checklist in PR).
4. Add to `TUI_RENDER_UNIT_SOURCES`.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__2[0-2]_" --output-on-failure
# regenerate after an intentional layout change (POSIX): UPDATE_GOLDEN=1 ctest --test-dir cmake-build -R "^tui__2[0-2]_"
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/20_snapshot_frame_and_dashboard_test.cpp` | `tui__20_snapshot_frame_and_dashboard_test` | `unit` | 7 |
| `tests/unit/tui/21_snapshot_links_and_detail_test.cpp` | `tui__21_snapshot_links_and_detail_test` | `unit` | 9 |
| `tests/unit/tui/22_snapshot_tables_browser_test.cpp` | `tui__22_snapshot_tables_browser_test` | `unit` | 6 |

## Constraints

- Goldens are mono/ASCII, 80x24, fixed clock (subtask 23 rules).
- No test may call anything but `MockCommandClient`; no registry, no repository.
- 3 test files each at most about 300 lines; fixtures shared via `fixtures.hpp`.

## Success criteria

1. Goldens exist for D1, D2, D3, D4, D6 and the listed empty/error/loading variants (22 files).
2. Every golden was reviewed against the ADR diagram; differences are documented in the PR or fed back as ADR amendments.
3. Requests asserted exactly for paging, sorting and filtering.

## Out of scope / risks

- Color/theme variants (`dark`, `light`) not snapshotted (mono only).
- Risk: churn while 06.0 screens evolve; mitigated by landing these tests in the same PR as each 06.0 screen subtask (incremental).
