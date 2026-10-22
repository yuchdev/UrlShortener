# 13 - Link detail, stats and preview screen (D4)

**Parent task:** 06.0 Terminal UI
**State:** ⬜ Not started
**Depends on:** 06.0/03, 04, 06, 08, 11 (result viewer), 12; 03.0/08 (`link.get`, `link.stats`, `link.preview` result shapes), 03.0/11 (registered)
**Blocks:** 06.0/18, 20
**Estimated size:** M
**Suggested agent:** cpp-expert

## Objective

Render **D4**: the detail view of one link from `link.get`, the 7-day stats
strip from `link.stats` (bucket `day`, text sparkline with weekday labels), and
the `link.preview` result on demand. Implements the D11 edge
`LinkDetail -> LinkForm` (`e`) and the `[e]dit [s]tats` hints of the window
title. Two requests run concurrently and render independently, so a failing
stats call never hides the link fields.

## Dependencies

- Internal: 06.0/03 (`AsyncCaller`), 06 (toasts), 08 (`FormScreen` for edit), 11 (`ResultViewer` for preview), 12 (navigation from the list).
- Commands: `link.get {slug}`, `link.stats {slug, from, to, bucket}`, `link.preview {slug}`.
- JSON fields: `link.get` returns the `LinkView` JSON of `serializeLinkViewJson` (verified keys: `id`, `slug`, `url`, `short_url`, `created_at`, `updated_at`, `status`, `redirect_type`, `tags`, `metadata`, `campaign`, `stats{...}`; **no** `expires_at`/`enabled`/`deleted_at` keys); `expires_at`, `enabled` and `deleted_at` are shown from the `link.preview` body (keys `slug,url,status,redirect_type,enabled,expires_at,deleted_at`, catalog 3.1), which this screen requests alongside `link.get`;
  `link.stats` without `from`/`to`/`bucket` returns the lifetime counters, with all three (epoch seconds, `bucket` `hour|day|week`) the range report (catalog 3.1, Task 03.0 subtask 08); `LinkStatsView` fields `total_attempts`, `successful_redirects`, `time_buckets`, `from`, `to`, `bucket` verified there; the element shape of `time_buckets` (`TimeBucketView`) and `status_code_counts` is **UNVERIFIED** - read `TimeBucketView` before coding (assumed `{start|bucket, count}`).
- Time source: `core/clock.hpp` `IClock`/`ManualClock` (injected) to compute `from = now - 7d` as decimal epoch seconds.
- External FTXUI: `window`, `hbox`, `vbox`, `text`, `separator`, `gauge`/block glyphs for the sparkline (mono -> `#`), `yframe`.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/tui/link_detail_model.hpp` / `src/tui/link_detail_model.cpp` | create | `LinkDetailModel`, `sparkline`, field layout (model lib) |
| `src/tui/screens/link_detail_screen.cpp` | create | `LinkDetailScreen : IScreen` (UI lib) |
| `tests/unit/tui/13_link_detail_model_test.cpp`, `13_link_detail_screen_test.cpp` | create | see Tests |
| `tests/unit/tui/golden/d04_detail_80x24.txt`, `d04_detail_stats_error_80x24.txt`, `d04_detail_deleted_80x24.txt`, `d04_detail_no_stats_80x24.txt`, `d04_detail_mono_80x24.txt` | create | goldens |
| `sources.cmake` | modify | register |

## Classes and functions

```cpp
struct DetailRow { std::string left_label, left_value, right_label, right_value; };
std::string sparkline(const std::vector<std::uint64_t>& values, std::size_t width, bool ascii);
//  levels "........" scaled to max; all-zero -> flat baseline; width 0 -> ""; ascii=true uses " .:-=+*#"
class LinkDetailModel {                                   // NEW, FTXUI-free
public:
    explicit LinkDetailModel(const core::Clock& clock);   // Clock type UNVERIFIED
    commands::CommandRequest get_request(const std::string& slug, const std::string& id) const;
    commands::CommandRequest stats_request(const std::string& slug) const;   // from = now-7d, to = now (decimal Unix epoch seconds), bucket="day"
    void apply_link(const std::string& link_json);
    void apply_stats(const std::string& stats_json);
    void apply_link_error(const std::string& code, const std::string& message);
    void apply_stats_error(const std::string& code, const std::string& message);
    std::vector<DetailRow> rows() const;                  // D4 two-column grid: id/short URL, target, status/redirect, created/expires, tags/campaign, metadata
    std::string stats_header() const;                     // "Stats (7d)  attempts 84,210  success 83,720  [bucket: day]"
    std::vector<std::pair<std::string, std::uint64_t>> day_bars() const;      // weekday label + count
};
class LinkDetailScreen final : public IScreen { /* id() == ScreenId::link_detail; keys e, s, p, r, q */ };
```

Number formatting: thousands separators (`84,210`) via a pure `format_count`; timestamps displayed as received (ISO-8601 UTC), never re-zoned.

## Implementation steps

1. `on_enter(slug)`: submit `link.get`, `link.preview` (for `enabled`/`expires_at`/`deleted_at`, absent from `LinkView` JSON) and `link.stats` together; render skeleton "loading..." for the panes.
2. Left/right columns layout exactly as D4 (labels dim, values normal); status text `active (enabled)` / `deleted (disabled)` composed from `status` (link.get) + `enabled` (link.preview); `campaign` rendered `name / source / medium`; absent -> `-`.
3. Stats pane: header string + one row of bars `Mon ####### Tue ########...` built by `day_bars()` and `sparkline` per day scaled to a fixed width; weekday computed from the bucket timestamp (UTC) with a small pure helper (no `<chrono>` calendar API in C++17 -> use `std::gmtime`-free civil-date algorithm; unit-tested).
4. `e` pushes `link_form` (`link.update` prefilled from the fetched JSON); `p` calls `link.preview` and opens the result viewer; `s` re-fetches stats with a toggle between buckets `day`/`hour` if the descriptor's `bucket` doc allows (otherwise key hidden - read the arg doc at runtime).
5. `deleted` links: show `deleted_at`, hint `u restore` that delegates to the list's action helper (reuse `run_mutation` with `link.restore`).
6. `q`/Esc pops to Links, preserving the list's selection/page (router keeps the Links screen instance alive).

## Commands

```powershell
cmake --build cmake-build --target tui__13_link_detail_model_test tui__13_link_detail_screen_test
ctest --test-dir cmake-build -R "^tui__13_" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `13_link_detail_model_test.cpp` | `tui__13_link_detail_model_test` | unit | `stats_request` bytes with `ManualClock` (from/to as epoch-second strings, `bucket:"day"`); `sparkline` scale cases: empty, one value, all zero, huge values (2^63), width 1; ascii variant; `format_count` 0/999/1,000/84,210; `rows()` for full link, link without tags/campaign/expires, deleted link; weekday computation across month/year boundaries and leap day |
| `13_link_detail_screen_test.cpp` | `tui__13_link_detail_screen_test` | unit | exactly two requests (`link.get`, `link.stats`) on enter; D4 golden; stats error leaves link pane intact and shows `stats unavailable: <code>`; `link.get` `not_found` -> toast and auto-pop; `e` opens `link.update` form prefilled; `p` shows preview JSON; mono golden is ASCII |

## Constraints

- C8: JSON only through `json_view` (over the 01.0/01 scanner); no `LinkView` DTO.
- D4 layout per ADR 0003; no raw identifiers beyond what the command returns (C6: stats are aggregates only, no client ids - the screen must not display `domain_counts`' raw referrers beyond what `LinkStatsView` already returns; v1 shows only attempts/success/time buckets).
- Time-dependent code uses the injected clock only (deterministic goldens).

## Success criteria

1. `d04_detail_80x24.txt` matches D4's structure (header with `[e]dit [s]tats`, two-column grid, stats strip).
2. Failure of either request never blanks the other pane.
3. All 7 day labels appear in Monday-first order starting from the 7-day window's first day (documented; D4 shows Mon..Sun for an example week).
4. `ctest -R tui__13_` passes with a fixed clock on Windows and POSIX.

## Out of scope / risks

- Per-domain/status-code breakdown panes, hourly drill-down charts, graph beyond bar sparklines (non-goal).
- D4's Mon..Sun labelling assumes the window aligns to a week; a rolling 7-day window starts on an arbitrary weekday - rendering uses the actual weekday of each bucket, so the example in D4 is illustrative (note for the ADR).
