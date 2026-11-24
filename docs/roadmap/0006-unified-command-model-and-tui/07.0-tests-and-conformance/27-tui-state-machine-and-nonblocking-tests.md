# 27 - TUI state machine and non-blocking tests

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22, 23; Task 06.0 (router, hotkeys, worker posting)
**Blocks:** 29, 34, 08.0/11
**Estimated size:** M
**Suggested agent:** testing-expert

> **Amended 2026-11-20 (as-built reconciliation).** There is **no
> `App::currentScreen()`** and no way to render `App` headlessly (`App::run()`
> starts the FTXUI loop). Navigation legality is proven through
> **`Router::is_legal`** (shipped `tui__04_router_test`, exhaustive over D11) and
> the shipped **`tui__20_app_navigation_test`**; assert on the `Router` state and
> on `MockCommandClient::recorded_requests()`, not on a current-screen accessor
> or a frame marker. Async non-blocking is proven with the shipped
> **`InlinePoster`** (`run_pending`) plus `MockCommandClient::set_delay` and a
> real `AsyncCaller` worker thread - **no fake clock**. Consumed: `tui__03`,
> `tui__04_router`, `tui__04_app_teardown`, `tui__16_poll_ticker`,
> `tui__20_app_navigation`. Gaps -> new stems `tui__31/32/33`: the
> requests-per-transition table, hotkey/palette completeness, and real-thread
> render-under-latency.

## Objective

Verify ADR 0003 D11 (screen state machine) transition by transition against
`Router::is_legal` and the requests each transition issues, and ADR 0003 rule 3
(the UI thread never blocks on a command): a slow `ICommandClient` must not
freeze the posted-response drain.

## Coverage already delivered (06.0)

| Need | Delivered by (consumed) |
|---|---|
| D11 legality exhaustive | `tui__04_router_test` (`Router::is_legal`) |
| navigation smoke across tabs | `tui__20_app_navigation_test` |
| async cancel/epoch/alive-guard, teardown ordering | `tui__03_async_caller_test`, `tui__04_app_teardown_test` |
| timer-driven job polling | `tui__16_poll_ticker_test` |

## Dependencies

- Subtasks 22, 23 (`tui_test_support.hpp`, shipped `render_to_text`/`send_events`/`InlinePoster::run_pending`); 06.0/04 `Router::is_legal` and screen ids.
- 06.0: `Router` edge set (for the completeness check), the hotkey map from D1/D3, `AsyncCaller` + `InlinePoster`. No current-screen accessor is required or exists.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/31_screen_transition_requests_test.cpp` | create | D11 transitions vs `Router::is_legal` + requests issued |
| `tests/unit/tui/32_hotkeys_and_focus_test.cpp` | create | hotkey table, focus rules, palette completeness |
| `tests/unit/tui/33_nonblocking_ui_test.cpp` | create | latency/threading via `InlinePoster` + real `AsyncCaller` thread |
| `sources.cmake` | modify | Append to the owning TUI list (06.0/01); render-coupled files go to `TUI_RENDER_UNIT_SOURCES`, model-only to `TUI_MODEL_UNIT_SOURCES` |

## Classes and functions

State machine (file 31): table-driven from the D11 mermaid; each row is `{from_screen, keys, to_screen, requests_expected}`, checked against `Router::is_legal` and `recorded_requests()`:

| From | Keys | To | Requests sent |
|---|---|---|---|
| Dashboard | `2` | Links | `link.list` |
| Dashboard | `3` | Tables | `db.tables` |
| Dashboard | `4` | ImportExport | none |
| Dashboard | `5` | Jobs | `job.list` |
| Dashboard | `:` | Palette | `command.list` (once per session, then cached) |
| Links | Enter | LinkDetail | `link.get`, `link.stats` |
| Links | `n` / `e` | LinkForm | none (`e` prefetches `link.get`) |
| LinkDetail | `e` | LinkForm | none |
| LinkForm | submit (mutating) | Confirm | none |
| Confirm | ok | Links | the `link.*` mutation then `link.list` refresh |
| Tables | Enter | RowJson | none |
| Tables | `n` (links table) | LinkForm | none |
| ImportExport | Start | Jobs | `link.import` |
| Palette | pick command | GenericForm | none |
| GenericForm | submit (mutating) | Confirm | none |
| Jobs | Enter | JobDetail | `job.get` |

Plus negative/edge cases: `Esc`/`q` goes back one level and never exits from
a nested screen; `Ctrl-C` quits from anywhere (App signals quit flag);
unmapped keys are ignored; navigation key typed inside a text `Input` is text,
not navigation (focus rule); switching screens during an in-flight request
discards the stale response (no render of the old screen's data on the new
screen); `n` on the `click_events` table does nothing (non-writable);
`n` in read-only mode shows disabled with the registry's reason (detail in 28).

Hotkeys (file 32): every hotkey listed in the D1/D3/D6/D8 footers exists in
the hotkey table and in the help overlay (`F1`) text (shipped
`tui__05_help_model_test` is the help-model source of truth); no two screens
bind the same key to different meanings within one screen; every action
reachable by hotkey is also in the palette.

Non-blocking (file 33), using `InlinePoster` + a real `AsyncCaller` thread:

| Case | Assertion |
|---|---|
| `slow_command_does_not_block_render` | `set_delay(500 ms)` for `link.list` on the mock; the event-handling + `render_to_text` path returns within a generous bound (10x CI margin, `steady_clock`) and shows the loading state while the worker is still delayed |
| `input_accepted_while_request_in_flight` | type into the filter while a request is pending (not yet `run_pending`-drained); text appears |
| `response_posted_exactly_once` | after `InlinePoster::run_pending`, the frame shows data and `recorded_requests()` count is 1 |
| `stale_response_discarded` | two quick refreshes: the slower first response, when drained, is dropped by `AsyncCaller`'s epoch stale-drop; the latest request's data shows |
| `job_polling_is_timer_driven_not_blocking` | no blocking wait in the event handler; `PollTicker` drives polling (shipped `tui__16_poll_ticker_test` is the ticker source of truth) |
| `shutdown_waits_for_workers` | `AsyncCaller`/screen teardown with a request in flight returns without deadlock within 2 s and the mock records no use-after-destroy (sanitizer build advisory; mirrors `tui__04_app_teardown_test`) |
| `client_exception_is_contained` | a mock client that throws turns into an error toast `internal`, no crash |

## Implementation steps

1. Implement file 31 from an array literal mirroring D11; assert each edge against `Router::is_legal` and the requests via `recorded_requests()`; keep the mermaid and the table adjacent in a comment.
2. Implement file 32 by reading the hotkey table object and the help model, not by scraping rendered text.
3. Implement file 33 with `MockCommandClient::set_delay`, the shipped `InlinePoster` (`run_pending`) and a real `AsyncCaller` worker thread - the only tests here that use real threads; keep each under 1 s. No fake clock.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__3[1-3]_" --output-on-failure
ctest --test-dir cmake-build -R "^tui__3[1-3]_" --repeat until-fail:20    # flake check once before merge
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/31_screen_transition_requests_test.cpp` | `tui__31_screen_transition_requests_test` | `unit` | table (16 transitions) + 6 edge cases |
| `tests/unit/tui/32_hotkeys_and_focus_test.cpp` | `tui__32_hotkeys_and_focus_test` | `unit` | 4 |
| `tests/unit/tui/33_nonblocking_ui_test.cpp` | `tui__33_nonblocking_ui_test` | `unit` | 7 |

## Constraints

- Timing assertions use generous bounds and relative comparisons (no absolute millisecond SLAs on shared CI).
- No `sleep`-based synchronization except bounded polling loops with a 2 s deadline.
- No terminal, no real FTXUI `ScreenInteractive::Loop` (screens/components are driven by direct `OnEvent` calls and `InlinePoster::run_pending`).

## Success criteria

1. Every D11 edge in `Router::is_legal` has a test row; a new transition without a row fails a completeness check (assert the Router edge set equals the test's set).
2. 20 repeated runs are green.
3. A deliberate synchronous `sleep` in a screen handler makes `slow_command_does_not_block_render` fail.

## Out of scope / risks

- Real terminal resize events: manual checklist.
- Risk: flaky timing tests; mitigated by repeat-run gate in subtask 34.
