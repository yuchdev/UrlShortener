#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/tui/panel_state.hpp>

namespace url_shortener::tui
{

/**
 * @file dashboard_model.hpp
 * @brief The FTXUI-free model behind the Dashboard landing screen (D2, subtask
 * 06.0/17): @ref DashboardModel builds the independent read-only requests that
 * feed the six panels, routes each response back to its panel, and exposes the
 * derived counters, sample rankings, recent-changes list and the jobs/backend
 * status lines.
 *
 * Everything here belongs to `url_shortener_tui_model` (ADR 0003 rule 6): no
 * FTXUI, no colour, no threads, no clocks. JSON is read only through the
 * sanctioned `json_view` helpers (contract C8/C9). None of these functions
 * sanitize for a terminal: every server/stored string (slug, status, command,
 * backend labels) is sanitized at the render sink by the screen (threat model
 * T-110). Every server-derived list/number is bounded here (threat model
 * T-121): the sample is capped at @ref DashboardModel::k_max_sample entries and
 * the recent/top lists at @ref DashboardModel::k_max_recent /
 * @ref DashboardModel::k_max_top.
 *
 * Honest-subset note (D2 "Data sources"): the command catalog has no aggregate
 * redirect/latency source today, so without a `stats.summary` command the
 * redirects/top-slugs figures are computed from a *sample* of the most recently
 * updated links and labelled as such; `success %` and `p50` render `n/a`. When
 * a `stats.summary` command exists (feature-detected by the screen) the model
 * reads the exact figures from it instead and drops the sample labelling.
 */

/**
 * @brief The link-count panel (D2 "Links total/active/disabled/deleted").
 *
 * Fed by four independent `link.list {limit:1}` calls (one per lifecycle
 * filter). The panel is @ref PanelState::unavailable if any of the four fails
 * (success criterion 2 - a single failing request degrades its panel), and
 * @ref PanelState::ready only once all four succeed.
 */
struct CountsPanel
{
    ///< The panel load state (see @ref CountsPanel).
    PanelState state = PanelState::loading;

    ///< `total` of `link.list {status:all}`: every link regardless of state.
    std::uint64_t total = 0;

    ///< `total` of `link.list {status:active}`.
    std::uint64_t active = 0;

    ///< `total` of `link.list {status:disabled}`.
    std::uint64_t disabled = 0;

    ///< `total` of `link.list {status:deleted}`.
    std::uint64_t deleted = 0;

    ///< `true` when the `total` count was not fetched because the session may
    ///< not run `link.list {status:all}` (it raises to write, D16): the count
    ///< renders `n/a (read-only)` instead of `0` (subtask 18 review A).
    bool total_restricted = false;

    ///< `true` when the `deleted` count was not fetched because the session may
    ///< not run `link.list {status:deleted}` (raises to write); renders
    ///< `n/a (read-only)`.
    bool deleted_restricted = false;
};

/**
 * @brief The redirect-metrics panel (D2 "Redirects (24h) / success / p50").
 *
 * Fed by the single sample `link.list` call (then @ref sampled is `true`, only
 * @ref redirects_24h is populated as the sum over the sample, and
 * @ref success_pct / @ref p50_ms stay empty) or, when a `stats.summary` command
 * is present, by that command's exact figures (then @ref sampled is `false` and
 * all three may be populated).
 */
struct RedirectsPanel
{
    ///< The panel load state; also governs the "Top slugs" section, which is
    ///< fed by the same call.
    PanelState state = PanelState::loading;

    ///< `true` when @ref redirects_24h is a sample estimate (no `stats.summary`
    ///< command), so the screen labels it `"(sample)"`; `false` for an exact
    ///< `stats.summary` figure.
    bool sampled = true;

    ///< The 24-hour redirect count: the sum of each sampled link's
    ///< `stats.redirects_24h`, or the exact `stats.summary` figure; empty when
    ///< the feeding call has not answered or failed.
    std::optional<std::uint64_t> redirects_24h;

    ///< The redirect success percentage (0..100); only ever populated from
    ///< `stats.summary` (no source in the sampled path), so it renders `n/a`
    ///< today.
    std::optional<double> success_pct;

    ///< The median (p50) redirect latency in milliseconds; only ever populated
    ///< from `stats.summary`, so it renders `n/a` today.
    std::optional<double> p50_ms;
};

/**
 * @brief One "Top slugs" entry: a slug and its redirect count.
 */
struct TopSlug
{
    ///< The link slug (raw server string; sanitized by the screen, T-110).
    std::string slug;

    ///< The slug's redirect count used for ranking and bar scaling.
    std::uint64_t count = 0;
};

/**
 * @brief One "Recent changes" row: the change time, slug and lifecycle status.
 *
 * Built from a `link.list {sort:updated_at, order:desc}` row. Per the honest
 * subset (no audit command), it shows the slug and status at `updated_at`, not
 * a command name (D2's `link.create docs2 ok` has no source).
 */
struct RecentChange
{
    ///< The `updated_at` time as UTC `HH:MM` exactly as received (no time-zone
    ///< conversion); empty when the timestamp could not be read.
    std::string time_hhmm;

    ///< The row's slug (raw server string; sanitized by the screen, T-110).
    std::string slug;

    ///< The row's lifecycle status (`active`, `disabled`, ...); raw server
    ///< string, sanitized by the screen.
    std::string status;
};

/**
 * @brief Builds and folds the six independent Dashboard panels (D2, subtask
 * 06.0/17).
 *
 * Construct with whether the catalog offers a `stats.summary` command;
 * @ref refresh_requests then returns the read-only calls to issue (each tagged
 * with its panel via a request-id suffix), and @ref apply folds one response
 * into the panel named by @ref panel_of. Every accessor is a deterministic,
 * headless function of the applied responses, so the whole dashboard is
 * unit-tested without a terminal (ADR 0003 rule 6). The model issues only read
 * commands and can never construct a mutating one (success criterion 3).
 *
 * Thread-safety: not synchronized; created, mutated (via @ref apply /
 * @ref start_refresh) and read only on the UI thread.
 */
class DashboardModel
{
public:
    /**
     * @brief Upper bound on links materialized from the sample `link.list`
     * page, so an over-returning server cannot inflate ranking work (T-121).
     * Matches the sample request's `limit:200`.
     */
    static constexpr std::size_t k_max_sample = 200;

    /**
     * @brief Upper bound on "Recent changes" rows retained (T-121); matches the
     * recent request's `limit:5`.
     */
    static constexpr std::size_t k_max_recent = 5;

    /**
     * @brief Number of "Top slugs" entries exposed (the top N of the sample).
     */
    static constexpr std::size_t k_max_top = 3;

    /**
     * @brief Builds the model.
     *
     * @param has_stats_summary `true` when the command catalog contains a
     * `stats.summary` command, selecting the exact-metrics request set over the
     * sampled one. Determined by the screen from `ICommandClient::commands()`.
     */
    explicit DashboardModel(bool has_stats_summary);

    /**
     * @brief Returns the independent read-only requests that feed the panels.
     *
     * One batch per refresh: four `link.list {limit:1}` counts calls, one
     * `link.list` recent-changes call, one sample/`stats.summary` call, a
     * `job.list {state:running}` call, a `system.info` call and a `db.tables`
     * call. Every request's `id` is `<next_id()>#<panel>` so a completion can
     * be routed back with @ref panel_of; the panel suffixes are the ones @ref
     * apply accepts. No request is a mutating command (success criterion 3).
     *
     * @param next_id A generator for a fresh, unique correlation id (e.g.
     * `RequestIdGenerator::next`); invoked once per request. Must not be empty.
     * @return The batch of requests, in a fixed order; the completion order is
     * irrelevant because @ref apply routes by panel.
     */
    std::vector<commands::CommandRequest> refresh_requests(
        const std::function<std::string()>& next_id) const;

    /**
     * @brief Resets the per-request bookkeeping for a new refresh batch.
     *
     * Call once before submitting a @ref refresh_requests batch. It clears the
     * per-counts success tracking so a transient failure from a previous batch
     * does not stick, but it deliberately keeps the last-known displayed values
     * and panel states so an auto-refresh does not flash `loading`.
     */
    void start_refresh();

    /**
     * @brief Marks a panel's request as not issued because the session may not
     * run it (subtask 18 review A).
     *
     * The dashboard screen calls this instead of submitting a request whose
     * effective permission the capability denies in a read-only session
     * (`link.list {status:all|deleted}` raises to write, D16). A restricted
     * count (`counts.total` / `counts.deleted`) no longer blocks the counts
     * panel from reaching @ref PanelState::ready and renders `n/a (read-only)`;
     * a restricted `recent` / `redirects` panel reports @ref restricted so the
     * screen shows `n/a (read-only)` rather than waiting forever. Call after
     * @ref start_refresh and before submitting the surviving batch.
     *
     * @param panel The panel name (as from @ref panel_of) whose request was not
     * issued.
     */
    void mark_restricted(const std::string& panel);

    /**
     * @brief Whether a panel's data was withheld because the session may not
     * run its request (read-only, review A).
     *
     * @param panel The panel name to query.
     * @return `true` when @ref mark_restricted was called for @p panel in the
     * current batch; `false` otherwise.
     */
    bool restricted(const std::string& panel) const;

    /**
     * @brief Returns the panel name a request/response id was tagged with.
     *
     * @param request_id An id produced by @ref refresh_requests (of the form
     * `<base>#<panel>`).
     * @return The substring after the last `'#'`, or an empty string when the
     * id carries no suffix (an unrelated id routes to no panel).
     */
    static std::string panel_of(const std::string& request_id);

    /**
     * @brief Folds one command response into the panel it belongs to.
     *
     * Dispatches on @p panel (as produced by @ref panel_of): a success response
     * updates that panel's data and advances its state toward
     * @ref PanelState::ready; a failure response marks only that panel
     * @ref PanelState::unavailable and records its reason, leaving the other
     * five untouched (success criterion 2). An unknown @p panel is ignored.
     *
     * @param panel The destination panel name (e.g. `"counts.total"`,
     * `"redirects"`, `"recent"`, `"jobs"`, `"backend"`, `"events"`).
     * @param response The response delivered on the UI thread; its result JSON
     * is read field-by-field via `json_view` (a missing/ill-typed field leaves
     * the member at its default).
     */
    void apply(const std::string& panel,
               const commands::CommandResponse& response);

    /**
     * @brief The link-count panel.
     * @return The counts panel; its @ref CountsPanel::state reports readiness.
     */
    const CountsPanel& counts() const { return m_counts_; }

    /**
     * @brief The redirect-metrics panel (also governs the Top slugs section).
     * @return The redirects panel.
     */
    const RedirectsPanel& redirects() const { return m_redirects_; }

    /**
     * @brief The ranked "Top slugs" entries (highest redirect count first).
     * @return At most @ref k_max_top entries; empty when the feeding call has
     * not succeeded or there are no links.
     */
    const std::vector<TopSlug>& top_slugs() const { return m_top_; }

    /**
     * @brief The "Recent changes" rows, newest first.
     * @return At most @ref k_max_recent rows; empty when the recent call has
     * not succeeded or there are no links.
     */
    const std::vector<RecentChange>& recent() const { return m_recent_; }

    /**
     * @brief The load state of the "Recent changes" panel.
     * @return @ref PanelState::ready after a successful recent call,
     * @ref PanelState::unavailable after a failure, else
     * @ref PanelState::loading.
     */
    PanelState recent_state() const { return m_recent_state_; }

    /**
     * @brief The failure reason for the counts panel.
     * @return The error message of the first failed counts call; empty unless
     * @ref counts is @ref PanelState::unavailable. Raw server text, sanitized
     * by the screen.
     */
    const std::string& counts_error() const { return m_counts_error_; }

    /**
     * @brief The failure reason for the redirects / top-slugs panel.
     * @return The feeding call's error message; empty unless @ref redirects is
     * @ref PanelState::unavailable. Raw server text, sanitized by the screen.
     */
    const std::string& redirects_error() const { return m_redirects_error_; }

    /**
     * @brief The failure reason for the recent-changes panel.
     * @return The recent call's error message; empty unless @ref recent_state
     * is
     * @ref PanelState::unavailable. Raw server text, sanitized by the screen.
     */
    const std::string& recent_error() const { return m_recent_error_; }

    /**
     * @brief The running-jobs status line (D2 "Jobs: ...").
     * @return `"Jobs: none running"` when no job is running, `"Jobs: 1 running
     * (<command> <pct>%)"` for one, or `"Jobs: <n> running (<command> <pct>%)"`
     * for several (summarising the first). `"Jobs: n/a"` when the `job.list`
     * call failed, `"Jobs: ..."` (an ellipsis) before it answers. The embedded
     * command is raw server text, sanitized by the screen (T-110).
     */
    std::string jobs_line() const;

    /**
     * @brief The backend status line (D2 "Backend: ...  Cache: ...").
     * @return `"Backend: <metadata>  Cache: <cache>"` from `system.info`,
     * `"Backend: n/a"` when that call failed, or `"Backend: ..."` before it
     * answers. The backend labels are raw server text, sanitized by the screen.
     */
    std::string backend_line() const;

    /**
     * @brief Whether the metadata backend is persistent across restarts.
     * @return `system.info.persistent`; defaults to `true` (as the frame does)
     * so the "this session" / memory banner does not flash before the answer.
     */
    bool persistent() const { return m_persistent_; }

    /**
     * @brief The lifetime `click_events` row count (D2 context figure).
     * @return The `db.tables` row count for the `click_events` table; empty
     * when that call has not succeeded or the table is absent. Best-effort: a
     * failed `db.tables` call never degrades another panel.
     */
    const std::optional<std::uint64_t>& click_events() const
    {
        return m_click_events_;
    }

    /**
     * @brief Renders a single horizontal bar scaled to a maximum.
     *
     * Emits `round(value / max * width)` filled glyphs (clamped to `[0,
     * width]`)
     * - `#` when @p ascii, else the full-block U+2588 which the mono ASCII
     * filter folds to `#`. Scaling uses `long double` so a count near `2^63`
     * cannot overflow. A @p width or @p max of `0`, or a `value` of `0`, yields
     * an empty string (a flat/absent bar).
     *
     * @param value The bar's value.
     * @param max The series maximum the bar is scaled against.
     * @param width The maximum number of glyphs (full bar at `value == max`).
     * @param ascii `true` for the `#` glyph (mono), `false` for U+2588.
     * @return The bar string, at most @p width display glyphs.
     */
    std::string bars(std::uint64_t value,
                     std::uint64_t max,
                     std::size_t width,
                     bool ascii) const;

private:
    /// Per-sub status of one counts request within the current batch.
    enum class SubStatus
    {
        pending,  ///< Not yet answered in this batch.
        ok,  ///< Answered successfully.
        failed,  ///< Answered with a failure.
        restricted  ///< Not issued: the session may not run it (read-only,
                    ///< D16).
    };

    /// Index of each counts sub-request into @ref m_counts_status_.
    enum CountsIndex
    {
        counts_total = 0,
        counts_active = 1,
        counts_disabled = 2,
        counts_deleted = 3,
        counts_sub_count = 4
    };

    /// Folds one counts sub-response into @p slot and recomputes the panel
    /// state (unavailable if any sub failed, ready once all four are ok).
    void apply_counts(CountsIndex slot,
                      std::uint64_t* field,
                      const commands::CommandResponse& response);

    /// Folds the sample `link.list` response into the redirects + top panels.
    void apply_sample(const commands::CommandResponse& response);

    /// Folds a `stats.summary` response into the redirects + top panels.
    void apply_stats_summary(const commands::CommandResponse& response);

    /// Folds the recent-changes `link.list` response into @ref m_recent_.
    void apply_recent(const commands::CommandResponse& response);

    ///< `true` to issue the exact-metrics request set (a `stats.summary` call)
    ///< instead of the sampled one; fixed at construction.
    bool m_has_stats_summary_;

    ///< The link-count panel.
    CountsPanel m_counts_;

    ///< Per-sub status of the four counts requests in the current batch.
    SubStatus m_counts_status_[counts_sub_count] = {SubStatus::pending,
                                                    SubStatus::pending,
                                                    SubStatus::pending,
                                                    SubStatus::pending};

    ///< The first failed counts call's error message.
    std::string m_counts_error_;

    ///< Panels whose request was withheld this batch because the session may
    ///< not run it (read-only, review A); drives @ref restricted.
    std::set<std::string> m_restricted_;

    ///< The redirect-metrics panel (and Top slugs governor).
    RedirectsPanel m_redirects_;

    ///< The ranked Top slugs (highest first), at most @ref k_max_top.
    std::vector<TopSlug> m_top_;

    ///< The redirects/top feeding call's error message when unavailable.
    std::string m_redirects_error_;

    ///< The Recent changes rows, newest first, at most @ref k_max_recent.
    std::vector<RecentChange> m_recent_;

    ///< The Recent changes panel load state.
    PanelState m_recent_state_ = PanelState::loading;

    ///< The Recent changes feeding call's error message when unavailable.
    std::string m_recent_error_;

    ///< The raw `job.list` result JSON (verbatim); empty until it answers.
    std::string m_jobs_json_;

    ///< The `job.list` load state; drives whether @ref jobs_line shows data,
    ///< `"..."` (loading) or `"n/a"` (failed).
    PanelState m_jobs_state_ = PanelState::loading;

    ///< The decoded metadata backend label from `system.info`.
    std::string m_backend_metadata_;

    ///< The decoded cache backend label from `system.info`.
    std::string m_backend_cache_;

    ///< The `system.info` load state; drives @ref backend_line.
    PanelState m_backend_state_ = PanelState::loading;

    ///< `system.info.persistent`; defaults to `true` (no banner flash).
    bool m_persistent_ = true;

    ///< The lifetime `click_events` row count from `db.tables` (best-effort).
    std::optional<std::uint64_t> m_click_events_;
};

}  // namespace url_shortener::tui
