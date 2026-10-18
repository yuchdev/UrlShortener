# 27 - TUI state machine and non-blocking tests

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 22, 23; Task 06.0 (router, hotkeys, worker posting)
**Blocks:** 29, 34, 08.0/11
**Estimated size:** M
**Suggested agent:** testing-expert

## Objective

Verify ADR 0003 D11 (screen state machine) transition by transition with
synthetic key events, and ADR 0003 rule 3 (the UI thread never blocks on a
command): a slow `ICommandClient` must not freeze rendering or input.

## Dependencies

- Subtasks 22, 23 (`press_keys`, `render_to_text`, mock latency); 06.0/04 `Router::is_legal` and screen ids.
- 06.0 hooks: a way to name the current screen for assertions (`App::currentScreen() -> ScreenId` or a hidden marker in the frame; **UNVERIFIED** - prefer a test accessor over parsing frames), a deterministic executor/`drain` for worker responses, and the hotkey map from D1/D3.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/tui/50_screen_state_machine_test.cpp` | create | D11 transitions |
| `tests/unit/tui/51_hotkeys_and_focus_test.cpp` | create | hotkey table, focus rules |
| `tests/unit/tui/52_nonblocking_ui_test.cpp` | create | latency/threading |
| `sources.cmake` | modify | Append to `TUI_RENDER_UNIT_SOURCES` (owned by Task 06.0 subtask 01) |

## Classes and functions

State machine (file 50): table-driven from the D11 mermaid; each row is `{from_screen, keys, to_screen, requests_expected}`:

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

Hotkeys (file 51): every hotkey listed in the D1/D3/D6/D8 footers exists in
the hotkey table and in the help overlay (`F1`) text; no two screens bind the
same key to different meanings within one screen; every action reachable by hotkey is also in the palette.

Non-blocking (file 52):

| Case | Assertion |
|---|---|
| `slow_command_does_not_block_render` | mock latency 500 ms for `link.list`; calling `OnEvent` and `render_to_text` returns within 50 ms (measured with `steady_clock`, generous 10x margin for CI) and shows the loading state |
| `input_accepted_while_request_in_flight` | type into the filter while a request is pending; text appears |
| `response_posted_exactly_once` | after `drain`, frame shows data, request count is 1 |
| `concurrent_responses_ordered_by_request_id` | two quick refreshes: first response (slower) arrives after second; UI shows the latest request's data (stale discard) |
| `job_polling_is_timer_driven_not_blocking` | no blocking wait in the event handler; polling uses 1 s fake-clock ticks (shared with 25) |
| `shutdown_waits_for_workers` | `App` destruction with a request in flight returns without deadlock within 2 s and the mock records no use-after-destroy (sanitizer build advisory) |
| `client_exception_is_contained` | a mock client that throws turns into an error toast `internal`, no crash |

## Implementation steps

1. Add the 06.0 test hooks (listed in Dependencies) as explicit requirements on 06.0 subtask 01 if missing.
2. Implement table-driven test for 50 from an array literal mirroring D11; keep the mermaid and the table adjacent in a comment so reviewers can diff.
3. Implement 51 by reading the hotkey table object, not by scraping rendered text.
4. Implement 52 with mock latency and a real worker thread (not the deterministic executor) - these are the only tests that use real threads; keep each under 1 s.

## Commands

```bash
ctest --test-dir cmake-build -R "^tui__5[0-2]_" --output-on-failure
ctest --test-dir cmake-build -R "^tui__5[0-2]_" --repeat until-fail:20    # flake check once before merge
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/tui/50_screen_state_machine_test.cpp` | `tui__50_screen_state_machine_test` | `unit` | table (16 transitions) + 6 edge cases |
| `tests/unit/tui/51_hotkeys_and_focus_test.cpp` | `tui__51_hotkeys_and_focus_test` | `unit` | 4 |
| `tests/unit/tui/52_nonblocking_ui_test.cpp` | `tui__52_nonblocking_ui_test` | `unit` | 7 |

## Constraints

- Timing assertions use generous bounds and relative comparisons (no absolute millisecond SLAs on shared CI).
- No `sleep`-based synchronization except bounded polling loops with a 2 s deadline.
- No terminal, no real FTXUI `ScreenInteractive::Loop` (screens are driven by direct `OnEvent` calls).

## Success criteria

1. Every D11 edge exists as a test row; adding a screen transition in 06.0 without a row fails a completeness check (the transition table in the app exposes its edges; test asserts the sets are equal - **requires** that 06.0 expose the edge list; otherwise manual review).
2. 20 repeated runs are green.
3. A deliberate synchronous `sleep` in a screen handler makes `slow_command_does_not_block_render` fail.

## Out of scope / risks

- Real terminal resize events: manual checklist.
- Risk: flaky timing tests; mitigated by repeat-run gate in subtask 34.
