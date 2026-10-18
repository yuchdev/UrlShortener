# 16 - Jobs screen and polling (D8)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/03, 04, 06, 09, 10, 11; 05.0/05 (`job.get`, `job.list`, `job.cancel`)
**Blocks:** 06.0/17, 18, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Render **D8**: table of ID / COMMAND / STATE / PROGRESS (`######----  62%`) /
STARTED / FINISHED / OUTCOME, keys `Enter detail (full error envelope)`,
`c cancel`, `r refresh (auto 1s)`, and the job detail view (D11 edge
`Jobs -> JobDetail`). Implements the polling rule of ADR 0003 rule 3 ("jobs are
polled with `job.get` on a timer"): a `PollTicker` posts to the UI thread once
per second only while it is needed.

## Dependencies

- Internal: 06.0/03 (`AsyncCaller`, `IUiPoster`), 09-10 (table), 11 (`ResultViewer`), 06 (toast).
- Commands (catalog 3.4): `job.list {state?, limit?, offset?}` -> `{items:[job...], total}`; `job.get {id}` -> `{id, command, state, progress{done,total}, result?, error?, created_at, finished_at?}`; `job.cancel {id}`. States `queued|running|succeeded|failed|cancelled`.
- Field mapping: STARTED = `created_at` (catalog has no `started_at`; D8 column title kept `STARTED`; amend catalog or ADR if `started_at` is added); OUTCOME = `succeeded` -> summary from `result` (`rows`, `created`/`total`), `failed` -> `error.code`, `cancelled` -> `job_cancelled`.
- External FTXUI: via 10; timer via `std::thread` + `IUiPoster` (FTXUI has no timer). `ScreenInteractive::RequestAnimationFrame()` is not needed.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/jobs_model.hpp` / `src/tui/jobs_model.cpp` | create | `JobsModel`, `progress_bar`, `outcome_text`, `PollSchedule` (model lib) |
| `include/url_shortener/tui/poll_ticker.hpp` / `src/tui/poll_ticker.cpp` | create | `PollTicker` thread + condition variable (model lib; uses `IUiPoster`) |
| `src/tui/screens/jobs_screen.cpp` | create | `JobsScreen`, `JobDetailScreen` (UI lib) |
| `tests/unit/tui/16_jobs_model_test.cpp`, `16_poll_ticker_test.cpp`, `16_jobs_screen_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d08_jobs_80x24.txt`, `d08_jobs_empty_80x24.txt`, `d08_job_detail_failed_80x24.txt`, `d08_jobs_mono_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
std::string progress_bar(std::uint64_t done, std::uint64_t total, std::size_t width, bool ascii);
//  "######----  62%"; total==0 -> unknown: "----------   ?"; done>total clamps to 100%; width < 4 -> percent only
std::string outcome_text(const std::string& job_json);
bool is_terminal_state(const std::string& state);          // succeeded|failed|cancelled

class PollSchedule {                                        // NEW, pure
public:
    explicit PollSchedule(std::chrono::milliseconds base = std::chrono::milliseconds(1000),
                          std::chrono::milliseconds max_backoff = std::chrono::milliseconds(5000));
    std::chrono::milliseconds next_delay(bool last_call_ok);   // doubles up to max on error; resets on success
    bool should_poll(bool screen_visible, bool any_active_job) const;   // visible OR active jobs known
};
class PollTicker {                                          // NEW
public:
    PollTicker(IUiPoster& poster, std::function<void()> on_tick);      // on_tick runs on the UI thread
    void set_interval(std::chrono::milliseconds);  void start();  void stop();   // stop joins; idempotent
    ~PollTicker();
};
class JobsModel {
public:
    void apply_list(const std::string& job_list_result_json);          // rows + active-job tracking
    bool any_active() const;
    std::optional<std::string> selected_id() const;
    bool can_cancel_selected() const;                                  // queued|running only
    void focus_job(const std::string& id);                             // from Import screen
};
```

## Implementation steps

1. `on_enter`: `job.list {limit:50}`; start ticker (1 s). Each tick submits one `job.list` unless the previous is still in flight (skip tick, never queue).
2. `apply_list` updates the table (via 09), tracks `any_active`; `PollSchedule::should_poll` stops the ticker when the screen is hidden and no active jobs (the Dashboard keeps its own light poll, subtask 17).
3. `c`: `job.cancel {id}` (no typed confirm; catalog marks it `write` but not mutating-confirmed; a yes/no confirm is still used when not `--yes` because it is a state change - decision documented) then immediate refresh; cancelling a finished job returns its final state (no-op toast "already succeeded").
4. `Enter`: push `job_detail`, call `job.get {id}` and show `pretty_print` of the **full envelope** (`ok`, `result` or `error`) in the result viewer; auto-refresh while non-terminal.
5. Progress bar uses `progress_bar(done,total, 10, ascii)`; mono uses `#`/`-`.
6. Errors from polling: footer shows `! refresh failed (code)` and `PollSchedule` backs off; recover automatically.

## Commands

```powershell
cmake --build cmake-build --target tui__16_jobs_model_test tui__16_poll_ticker_test tui__16_jobs_screen_test
ctest --test-dir cmake-build -R "^tui__16_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `16_jobs_model_test.cpp` | `tui__16_jobs_model_test` | unit | `progress_bar` 0/total, 62%, 100%, total 0, done > total, widths 3/10/40; `outcome_text` for succeeded import (`312 rows`/`created`), failed (`invalid_url`), cancelled; `can_cancel_selected` per state; D8 sample list parsed (3 jobs); `PollSchedule` backoff sequence 1s,2s,4s,5s,5s then reset; `should_poll` truth table |
| `16_poll_ticker_test.cpp` | `tui__16_poll_ticker_test` | unit | with `InlinePoster` and 20 ms interval: >= 3 ticks in 100 ms; `stop()` prevents further posts and returns quickly; destruction while running; double `start`; tick callback runs on the "UI" thread only |
| `16_jobs_screen_test.cpp` | `tui__16_jobs_screen_test` | unit | D8 golden; polling does not overlap (slow mock); `c` on running job sends one `job.cancel` after confirm; `c` on succeeded job sends nothing; `Enter` shows detail golden with error envelope; `focus_job("j-9")` selects the row; read-only: cancel disabled (permission `write`) but list/detail work |

## Constraints

- Rule 3 (ADR 0003): all calls via `AsyncCaller`; the ticker thread never calls `ICommandClient`, it only posts a UI-thread tick.
- Polling must stop when idle (no busy loops while the app sits on another tab): test via `should_poll`.
- Jobs are in-memory server-side (Q8): a TUI restart shows an empty list - screen footer notes "jobs are not persistent".
- Layout per D8; mono supported.

## Success criteria

1. `d08_jobs_80x24.txt` matches D8 (columns, `>` marker, progress bars, footer).
2. At most one `job.list` is in flight at any time (mock asserts max concurrency 1).
3. Ticker thread count returns to baseline after leaving the screen (test uses `stop()`/destructor).
4. Failed job detail shows the complete error envelope.

## Out of scope / risks

- Persistent job history, log streaming, per-row progress animations, retry button.
- Risk: no `started_at` in the job record (STARTED = `created_at`); queued jobs would show a start time before they run. Show `-` while `state == queued` to avoid misleading output.
