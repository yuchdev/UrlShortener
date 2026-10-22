# 17 - Dashboard screen (D2)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/03, 04, 05, 06, 12, 16 (job summary helper); 03.0/14 (`link.list`), 03.0/15 (`db.tables`), 03.0/17 (`system.info`), 05.0/05 (`job.list`)
**Blocks:** 06.0/18, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Render **D2** as the landing screen: link counters (total / active / disabled /
deleted), redirect figures, "Top slugs" with bar glyphs, "Recent changes", the
running-jobs line and the backend / cache status line. The screen is a
read-only aggregation of ordinary commands; each panel loads independently so
one failing call degrades only its panel.

**Design problem found while specifying (must be accepted or fixed upstream):**
D2 shows data the command catalog cannot provide today: `Redirects (24h)`,
`success %`, `p50 redirect` latency, `Top slugs (24h)`, and `Recent changes`
with command names (`link.create docs2 ok`). There is no aggregate or audit
command (`link.stats` is per slug; `link.list` cannot sort by clicks;
latency is not stored). This subtask therefore implements an honest subset
(below) and feature-detects a future command.

## Dependencies

- Internal: 06.0/03 (`AsyncCaller`), 12 (`parse_filter_text` not needed; reuses `row_from_link_json`), 16 (`outcome_text`/`is_terminal_state` for the jobs line), 04-06 (frame, theme, toasts).
- Commands used: `link.list` with `limit:1` for `total` per status (5 calls: `all`, `active`, `disabled`, `expired`, `deleted`); `link.list {status:all, sort:updated_at, order:desc, limit:5}` for Recent changes (shows `updated_at`, slug, status; not the command name); `link.list {status:all, limit:200, sort:updated_at}` sampled for Top slugs / 24h redirects using `stats.redirects_24h` (JSON key verified in `src/http/handlers/link_handlers.cpp`), labelled "(sample of 200 most recently updated)"; `job.list {state:running}`; `system.info` (`backend`, `persistent`); `db.tables` for `click_events` count.
- Feature detection: if `ICommandClient::commands()` contains a command named `stats.summary` (PROPOSED, not in catalog), use it for redirects/success/p50/top slugs and drop the sampled labels. Name is a proposal for a follow-up milestone (not owned by any 0006 subtask); absent today.
- Cache status "ok" in D2: `system.info.backend.cache` shows the configured backend, **not** health; v1 shows the name only (no "(ok)").
- External FTXUI: `window`, `hbox`, `vbox`, `text`, `gauge`/block glyphs, `separator`, `flex`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/dashboard_model.hpp` / `src/tui/dashboard_model.cpp` | create | `DashboardModel`: six independent panels with `PanelState` (model lib) |
| `src/tui/screens/dashboard_screen.cpp` | create | `DashboardScreen : IScreen` (UI lib) |
| `tests/unit/tui/17_dashboard_model_test.cpp`, `17_dashboard_screen_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d02_dashboard_80x24.txt`, `d02_dashboard_partial_failure_80x24.txt`, `d02_dashboard_memory_80x24.txt`, `d02_dashboard_stats_summary_80x24.txt`, `d02_dashboard_mono_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
enum class PanelState { loading, ready, unavailable };           // unavailable shows "n/a" plus reason, never hides the panel
struct CountsPanel { PanelState state; std::uint64_t total = 0, active = 0, disabled = 0, deleted = 0; };
struct RedirectsPanel { PanelState state; bool sampled = true; std::optional<std::uint64_t> redirects_24h; std::optional<double> success_pct; std::optional<double> p50_ms; };
struct TopSlug { std::string slug; std::uint64_t count; };
struct RecentChange { std::string time_hhmm; std::string slug; std::string status; };
class DashboardModel {                                            // NEW
public:
    explicit DashboardModel(bool has_stats_summary);
    std::vector<commands::CommandRequest> refresh_requests(const std::function<std::string()>& next_id) const;   // ~8 requests, tagged by panel via request id suffix
    void apply(const std::string& panel, const commands::CommandResponse& r);
    const CountsPanel& counts() const;  const RedirectsPanel& redirects() const;
    const std::vector<TopSlug>& top_slugs() const;  const std::vector<RecentChange>& recent() const;
    std::string jobs_line() const;      // "Jobs: 1 running (link.import 62%)" or "Jobs: none running"
    std::string backend_line() const;   // "Backend: sqlite  Cache: redis"
    std::string bars(std::uint64_t value, std::uint64_t max, std::size_t width, bool ascii) const;
};
```

## Implementation steps

1. `refresh_requests` returns the independent calls; `DashboardScreen::on_enter` submits all through `AsyncCaller`; completion order is irrelevant.
2. Counts: `total` field of each `link.list` response (`limit:1` keeps payloads tiny). Thousands separators `1,204`.
3. Sample-based panels: sort the sample client-side by `stats.redirects_24h` desc, take 3, bars scaled to the max; success % and p50 render `n/a`.
4. Recent changes: from `updated_at` (HH:MM local? use UTC `HH:MM` as received; no time-zone conversion), text `10:42 updated  docs2  active`.
5. Auto refresh every 5 s while the Dashboard is visible (reuse `PollTicker` from 16 with a 5 s interval); `r` refresh now; `Enter` on a panel jumps to the related tab (`2` Links, `5` Jobs).
6. Memory backend: show the persistent banner via frame (04); additionally the counts panel suffix "(this session)".

## Commands

```powershell
cmake --build cmake-build --target tui__17_dashboard_model_test tui__17_dashboard_screen_test
ctest --test-dir cmake-build -R "^tui__17_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `17_dashboard_model_test.cpp` | `tui__17_dashboard_model_test` | unit | counts from four mock responses (1,204/1,180/9/15 as in D2); sample ranking and bar scaling (ties, zero max, huge counts); one failing panel -> `unavailable` while others `ready`; empty store (all zeros); jobs line for 0/1/2 running; `has_stats_summary` switches request set; request list contains no mutating command (assert permission strings) |
| `17_dashboard_screen_test.cpp` | `tui__17_dashboard_screen_test` | unit | goldens D2 (complete), partial failure, memory backend, stats.summary variant, mono; `r` triggers exactly one new batch; leaving the screen stops the 5 s timer; no calls beyond the allowed read commands |

## Constraints

- Only read commands; works in read-only sessions unchanged.
- Never presents sampled numbers as exact (label "(sample)") - correctness over matching D2's pretty figures.
- No raw client identifiers (C6); only slugs and aggregate numbers.
- C8: JSON via `json_view` (over the 01.0/01 scanner).

## Success criteria

1. `d02_dashboard_80x24.txt` has all D2 sections (counts line, redirects line, top slugs, recent changes, jobs/backend line) at 80x24.
2. A single failing request marks only its panel `n/a`.
3. All dashboard requests are read-only commands from the catalog (asserted).
4. With a `stats.summary` command present the sample labels disappear.

## Out of scope / risks

- Real latency (p50) and success-rate metrics, charts beyond bars, time-range selector, analytics deep-dives (Milestone 0002).
- ADR 0003 D2 now carries a "Data sources" note stating which figures have no source command; adding `stats.summary` is a follow-up outside Milestone 0006.
- Risk: 9 requests on every refresh could be slow on large tables: `ListLinksPage` (Task 03.0 subtask 01) computes `total` with an in-memory scan capped at `kListScanLimit` (100 000 rows) per call; refresh interval is conservative.
