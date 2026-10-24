# Task 06.0 - Terminal UI

**Parent milestone:** [Milestone 0006 - Unified Command Model and TUI](/docs/roadmap/0006-unified-command-model-and-tui/plan.md)
**Status:** ⬜ Not started
**Depends on:** 01.0, 02.0, 03.0, 05.0 (04.0 is not required: the TUI uses the in-process client over `build_command_runtime`)

## Scope

Implement the TUI of [ADR 0003](/docs/adr/0003-tui-framework-and-architecture.md)
with FTXUI: shell and navigation, a generic form builder driven by `ArgSpec`,
a paged table view, the bespoke Dashboard / Links / Link detail / Tables /
Import-Export / Jobs / Settings screens, the Command Palette (every command
reachable), confirm dialogs and toasts, read-only mode, a mono/ASCII theme, the
`tui` CLI mode, and a manual smoke checklist. The TUI talks only to
`ICommandClient`.

The work is split into 21 small subtasks so that each one has a single seam to
test: **pure view-models** (`url_shortener_tui_model`, no FTXUI, plain Boost.Test)
and **thin FTXUI renderers** (`url_shortener_tui`, snapshot-tested against golden
text files rendered through `ftxui::Screen`). Subtask 21 (Settings screen, ADR
0003 D13) was added 2026-10-22, after the headless design pass, at the user's
request; it is numbered after the manual-smoke-checklist subtask (20) even
though it must be *built* before that checklist runs - see subtask 20's
`Depends on`, which was updated to include it. Numbers are unique labels here,
not an execution order; `Depends on`/`Blocks` is authoritative.

### Depends on (task and subtask level)

| From | Needed by 06.0 subtasks | Why |
|---|---|---|
| 01.0/01 | 02 | the one JSON scanner (`scan_object`, `scan_array`); the TUI has no tokenizer of its own |
| 01.0/03-07 | 02, 18 | envelope types, `ArgSpec` (with `nullable`, object `fields`), `CommandDescriptor`, `CommandRegistry`, `execute` + `AccessGuard` |
| 02.0/10, 02.0/12 | 19 | the `--storage-config` flag helper and the backend bootstrap in `main` that `tui` reuses |
| Task 02.0 | 20 | persistent backend for the restart check |
| 03.0/08, 03.0/11 | 12, 13 | `link.*` commands registered; preview/stats result shapes |
| 03.0/14 | 12, 17 | `link.list` |
| 03.0/15, 03.0/16 | 14, 17 | `db.tables`, `db.rows` and the `links`/`click_events` sources |
| 03.0/17 | 17, 21 | `system.info` (17: status line; 21: the Settings screen's sole data source) |
| 03.0/18 | 19 | `composition::build_command_runtime` (the one registry builder) |
| 05.0/05 | 15, 16, 17 | `job.get/list/cancel` |
| 05.0/08, 05.0/10 | 15, 20 | `link.import`, `link.export` |
| 05.0/20 | 19 | process-wide job runtime in `build_command_runtime`, shutdown on quit |

Subtask ids of other tasks are the current ones (headers are checked for
symmetry mechanically).

## Dependency mini-graph (within 06.0)

```text
01 ftxui+cmake ──┬─ 02 client seam ──┬─ 03 async ─┬─ 04 shell/router ─ 05 theme+help ─ 06 toast/confirm
                 │                   │            │                                        │
                 │                   │            └─ 09 table model ─ 10 table view        │
                 │                   └─ 07 form model (needs 06) ─ 08 form widgets/screen ─┤
                 │                                                                         │
                 │                    11 palette+generic screen (02-08) ───────────────────┤
                 │                       ├─ 12 links list (09,10,11) ─ 13 link detail      │
                 │                       ├─ 14 tables browser (09,10)                      │
                 │                       ├─ 15 import/export                               │
                 │                       ├─ 16 jobs (09,10) ─ 17 dashboard (12,16)         │
                 │                       └─ 21 settings (03.0/17, no form/table needed)    │
                 │                                                                         │
                 └─ 19 tui CLI wiring (02,04,05) ──────────── 18 read-only (11-17) ─────── 20 smoke checklist
```

Critical path: 01 -> 02 -> 03 -> 04 -> 05 -> 06 -> 07 -> 08 -> 11 -> 12 -> 13 -> 18 -> 20.
After 08, the screens 12, 14, 15, 16 can be built in parallel; 09/10 can run in
parallel with 04-08; 19 can start once 04/05 exist. 21 (Settings) only needs
04/05 and 03.0/17, so it can build in parallel with everything from 06 onward;
unlike every other screen it does **not** block 18 (it has no write control
for 18 to verify - see 21's own Constraints), only 20.

## Subtasks

| # | Document | Status | Depends on | Blocks |
|---|----------|--------|------------|--------|
| 01 | [FTXUI dependency, CMake option, render harness](01-ftxui-dependency-and-cmake-option.md) | ⬜ Not started | none | 02, 03, 04, 19, 20 |
| 02 | [ICommandClient, InProcessClient, JSON view helpers, include boundary](02-command-client-seam.md) | ⬜ Not started | 01, 01.0/01, 01.0/03, 01.0/04, 01.0/05, 01.0/06, 01.0/07 | 03, 04, 06, 07, 09, 11, 18, 19, 20 |
| 03 | [Async call helper (worker + PostEvent)](03-async-call-helper.md) | ⬜ Not started | 01, 02 | 04, 06, 08, 09, 11, 12, 13, 14, 15, 16, 17, 20 |
| 04 | [App shell, frame (D1) and router (D11)](04-app-shell-frame-and-router.md) | ⬜ Not started | 01, 02, 03 | 05, 06, 08, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20 |
| 05 | [Theme (dark/light/mono) and help overlay](05-theme-and-help-overlay.md) | ⬜ Not started | 04 | 06, 08, 10, 11, 14, 17, 19, 20 |
| 06 | [Toast and confirm components (D10)](06-toast-and-confirm-components.md) | ⬜ Not started | 02, 03, 04, 05 | 07, 08, 11, 12, 13, 15, 16, 17, 18, 20 |
| 07 | [FormModel: ArgSpec to request](07-form-model.md) | ⬜ Not started | 02, 06 | 08, 11, 20 |
| 08 | [Form widgets and form screen (D5)](08-form-widgets-and-form-screen.md) | ⬜ Not started | 03, 04, 05, 06, 07 | 11, 12, 13, 14, 15, 18, 20 |
| 09 | [TableModel (paging, sort, filter, hscroll)](09-table-model.md) | ⬜ Not started | 02, 03 | 10, 12, 14, 16, 20 |
| 10 | [TableView renderer (D3/D6/D8 body)](10-table-view.md) | ⬜ Not started | 04, 05, 09 | 12, 14, 16, 20 |
| 11 | [Command Palette (D9) and generic command screen](11-command-palette-and-generic-command-screen.md) | ⬜ Not started | 02, 03, 04, 05, 06, 07, 08 | 12, 13, 14, 15, 16, 18, 20 |
| 12 | [Links list screen (D3)](12-links-list-screen.md) | ⬜ Not started | 03, 04, 06, 08, 09, 10, 11, 03.0/11, 03.0/14 | 13, 17, 18, 20 |
| 13 | [Link detail, stats, preview (D4)](13-link-detail-screen.md) | ⬜ Not started | 03, 04, 06, 08, 11, 12, 03.0/08, 03.0/11 | 18, 20 |
| 14 | [Tables browser (D6)](14-tables-browser-screen.md) | ⬜ Not started | 03, 04, 05, 08, 09, 10, 11, 03.0/15, 03.0/16 | 18, 20 |
| 15 | [Import / export pairs (D7)](15-import-export-screen.md) | ⬜ Not started | 03, 04, 06, 08, 11, 05.0/05, 05.0/08, 05.0/10 | 18, 20 |
| 16 | [Jobs screen and polling (D8)](16-jobs-screen.md) | ⬜ Not started | 03, 04, 06, 09, 10, 11, 05.0/05 | 17, 18, 20 |
| 17 | [Dashboard (D2)](17-dashboard-screen.md) | ⬜ Not started | 03, 04, 05, 06, 12, 16, 03.0/14, 03.0/15, 03.0/17, 05.0/05 | 18, 20 |
| 18 | [Read-only mode and mutation safety](18-read-only-mode.md) | ⬜ Not started | 02, 04, 06, 08, 11, 12, 13, 14, 15, 16, 17, 01.0/07 | 20 |
| 19 | [`tui` CLI mode wiring](19-tui-cli-mode-wiring.md) | ⬜ Not started | 01, 02, 04, 05, 02.0/10, 02.0/12, 03.0/18, 05.0/20 | 20 |
| 20 | [Manual smoke checklist and diagram sign-off](20-manual-smoke-checklist.md) | ⬜ Not started | 01-19, 21, Task 02.0, 05.0/08, 05.0/10 | 07.0/29, 08.0/10 |
| 21 | [Settings screen (D13)](21-settings-screen.md) | ⬜ Not started | 02, 03, 04, 05, 03.0/17 | 20 |

## Shared conventions (apply to every subtask)

- **K1 Layout.** Public headers `include/url_shortener/tui/`, implementations `src/tui/` and `src/tui/screens/`, namespace `url_shortener::tui`. ADR-given type names (`ICommandClient`, `InProcessClient`, `FormBuilder`, `TableView`) are kept; functions, members and enumerators are `lower_case`, private members `m_`-prefixed (`.clang-tidy`). Existing sources also use CamelCase type names, so type-name style follows the ADR/existing code.
- **K2 Two libraries, both created only under `URLSHORTENER_ENABLE_TUI`.** `url_shortener_tui_model` (links `url_shortener_common`, never FTXUI) and `url_shortener_tui` (links the model lib and `ftxui::component/dom/screen`). Sources are listed in `sources.cmake` as `TUI_MODEL_CPP_SOURCES` / `TUI_UI_CPP_SOURCES`, never in `COMMON_CPP_SOURCES`. `url_shortener` links `url_shortener_tui` and gets `URLSHORTENER_WITH_TUI=1` (or `0` when OFF).
- **K3 Tests.** `tests/unit/tui/NN_<name>.cpp` where `NN` is the subtask number (01-21, except 20 which produces no automated test file - manual checklist only; Task 07.0 uses 22-69 in the same folder, plan.md "Test file prefixes"); lists `TUI_MODEL_UNIT_SOURCES` and `TUI_RENDER_UNIT_SOURCES` in `sources.cmake`, registered by the macro `register_tui_tests` (subtask 01), because the existing `register_labeled_cpp_tests` links only `url_shortener_common`. These two lists and the macro are the **only** TUI test registration scheme; Task 07.0 appends to them and does not create a third list. Target names follow the repo rule `<parent_dir>__<stem>` (e.g. `tui__07_form_model_test`); label `unit`. CLI parser tests for `tui` live in `tests/unit/cli/30-39`.
- **K4 Golden snapshots.** Plain text produced by `testing::render_to_text` (no ANSI codes) compared with `tests/unit/tui/golden/*.txt`; regenerate with `UPDATE_GOLDEN=1` and review the diff. Golden file names start with the diagram id (`d01_`, `d02_`, ...).
- **K5 Mock seam.** Screens are tested with `MockCommandClient` + `InlinePoster` (tests/unit/tui/support/, created by 02 and 03; Task 07.0 subtask 22 extends the same mock, subtask 23 extends the 01 render harness); no terminal, registry or storage is needed.
- **K7 JSON.** Results are read with `json_view` (02), a thin layer over the commands-layer scanner (Task 01.0 subtask 01); no TUI code tokenizes JSON itself (plan C9).
- **K8 Composition.** `tui_entry.cpp` (19) obtains the registry from `composition::build_command_runtime` (plan C11) and initializes logging disabled or to a file before doing so.
- **K6 Verification markers.** `VERIFY AT PIN TIME` marks FTXUI API names that depend on the release pinned in subtask 01; `UNVERIFIED` marks repository symbols that do not exist yet (other 0006 tasks) or that were not inspected.

## Key constraints

- C8: no screen includes a service, repository, `http/` or DTO header - only `ICommandClient` and envelope types; the include-boundary test (02) enforces it, with `in_process_client.*` and `tui_entry.cpp` as the only allowed registry users.
- C5: `tui` mode never starts `HttpServer`, binds a socket, or touches `uri.txt`/`UriMapSingleton` (19 static test + 20 manual S16).
- C9: FTXUI is the only new dependency, optional (`URLSHORTENER_ENABLE_TUI`, default ON); no JSON library, C++17 only, every new `.cpp` in `sources.cmake`.
- Never block the UI thread (ADR 0003 rule 3): all calls go through `AsyncCaller` (03).
- Mutations confirm (rule 4, via the single `run_mutation` choke point in 08) unless `--yes`.
- Read-only mode disables, never hides, write controls (rule 5, subtask 18); authorization stays in the registry (C7).
- Layouts follow D1-D13; deviations amend ADR 0003 in the same change set. 80x24 minimum, `--theme mono` ASCII fallback.

## Exit criteria

1. `url_shortener tui` starts the D1 frame in Windows Terminal and a Linux terminal, and every command returned by `command.list` is runnable from the palette with a generated form.
2. All of D1-D13 are covered (matrix below) by an automated snapshot or model test, and by a manual item for the interactive diagrams.
3. `ctest --test-dir cmake-build -L unit -R "^tui__"` is green; `ctest -R "^cli__30_"` is green; the pre-existing unit/contract suites are unchanged.
4. `-DURLSHORTENER_ENABLE_TUI=OFF` configures and builds with no FTXUI fetch; `url_shortener tui` then exits 2 with a clear message.
5. Boundary tests pass: no forbidden includes under `src/tui/` (02), no `HttpServer`/`uri.txt` references in the TUI entry (19).
6. A read-only session sends zero write requests across all screens (18).
7. The manual checklist `docs/testing/tui_manual_smoke.md` is executed and recorded (20).

### Diagram coverage matrix

| Diagram | Content | Primary subtask(s) | Supporting subtask(s) | Golden / test evidence |
|---|---|---|---|---|
| D1 | Frame and navigation | 04 | 05 (theme), 18 (role badge) | `d01_frame_*.txt`, router test |
| D2 | Dashboard | 17 | 16, 12 | `d02_dashboard_*.txt` |
| D3 | Links table (`link.list`) | 12 | 09, 10 | `d03_links_*.txt`, `d03_table_20x_80x24.txt` |
| D4 | Link detail / stats / preview | 13 | 11 (viewer) | `d04_detail_*.txt` |
| D5 | New / edit link form | 08 | 07, 12 (launch) | `d05_*.txt`, `07_request_*.json` |
| D6 | Tables browser | 14 | 09, 10, 11 | `d06_tables_*.txt` |
| D7 | Import / export | 15 | 08, 16 | `d07_*.txt` |
| D8 | Jobs | 16 | 09, 10 | `d08_*.txt` |
| D9 | Command Palette | 11 | 02 (`commands()`), 18 | `d09_palette_*.txt` |
| D10 | Confirm and error toast | 06 | 08 (`run_mutation`) | `d10_*.txt` |
| D11 | Screen state machine | 04 (`Router::is_legal`) | 11-16 (edges exercised) | `04_router_test` (exhaustive edge table) |
| D12 | Sequence: create on every transport | 02 (client seam) | 03 (async hop), 08 (submit) | `02_in_process_client_test`, `08_form_screen_test` request bytes; cross-transport equality in 07.0 |
| D13 | Shortener settings | 21 | 03.0/17 (`system.info`) | `d13_settings_*.txt` |

## Findings and risks recorded while elaborating this task

- **ADR 0003 D2 is only partly satisfiable from the catalog**: redirects (24h), success %, p50 latency, top slugs and command-named "recent changes" have no source command. ADR 0003 now carries a "Data sources" note under D2; subtask 17 renders an honest sampled subset and feature-detects a proposed `stats.summary` (follow-up outside 0006).
- **D1 `[6]Analytics` tab**: ADR 0003 now states it is a placeholder in 0006; subtask 04 renders "provided by Milestone 0002".
- **`ArgSpec::nullable` and object `fields`** are specified in Task 01.0 subtask 05 and surfaced as `ArgInfo::nullable`/`members` (02); needed for the `link.update` clear semantics and the campaign sub-fields of D5.
- **ADR 0003 rule 4 `MIGRATE` confirmation** is now worded as a reserved rule for future `migrate`-permission commands (no such command in 0006); the confirm policy keeps an inert, unit-tested branch (06).
- **D7** now shows the catalog values `full_url,slug` and `overwrite` (15).
- **`LinkView` JSON has no `expires_at`/`enabled`/`deleted_at` keys** (verified `serializeLinkViewJson`); D3 `EXPIRES` shows `-`, the detail screen reads those fields from `link.preview` (12, 13).
- **`db.tables` columns carry `sortable`/`searchable`** (catalog 3.2, additive); there is still no insert-command hint, so subtask 14 hard-codes `links -> link.create` (14).
- **The job object has `started_at`** (catalog 3.4, 05.0/01 and 05.0/05); STARTED shows it, `-` while queued (16).
- **`--yes` skips the typed-slug confirmation of `link.delete`** because ADR 0003 says confirmations are skippable; flagged in 06/18.
- **Logging**: the TUI must not write to the console while the screen is active; verified that an uninitialized logger is enabled with a `stderr` sink, so 19 initializes logging to a file sink (default `./url_shortener-tui.log`, or `--log-file`), never the console (decision log D14).
- **Manual checklist location**: repo agents say `docs/test/`, the repository actually uses `docs/testing/`; this task uses `docs/testing/tui_manual_smoke.md` (20).
- Pinned FTXUI version, MSVC build, vcpkg port and Windows-console behaviour remain to be verified in subtask 01 (decision-log "Things to research").
- **Settings screen added 2026-10-22** (subtask 21, ADR 0003 D13), after the headless design pass, at the user's explicit request alongside the Command Palette (11, pre-existing) and Tables browser / "DB viewer" (14, pre-existing). It is read-only by design - `ServerConfig` has no runtime-mutation path (verified against `src/main.cpp`'s startup flow), so there is deliberately no `settings.set` command; see ADR 0003's Non-goals. Backed by an extension to `system.info` (03.0/17) adding a `shortener` block of explicitly-named, non-secret `ServerConfig` fields - verified against `include/url_shortener/core/config.hpp` field-by-field so no secret (`tls`, `analytics_client_hash_salt`) or network setting (ports, HSTS) is included.
