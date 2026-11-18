#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/commands/envelope.hpp>

class IClock;  // url_shortener core/clock.hpp (global namespace)

namespace url_shortener::tui
{

/**
 * @file link_detail_model.hpp
 * @brief The FTXUI-free model behind the Link detail / stats screen (D4,
 * subtask 06.0/13): @ref LinkDetailModel builds the `link.get` / `link.stats`
 * requests, parses their results, and exposes the D4 two-column grid, the
 * 7-day stats header and the per-day bar series.
 *
 * Everything here belongs to `url_shortener_tui_model` (ADR 0003 rule 6): no
 * FTXUI, no colour, no threads. The only clock it touches is the injected
 * @ref IClock, used solely to compute the stats time window (deterministic
 * under a `ManualClock`). JSON is read only through the sanctioned `json_view`
 * helpers (contract C8/C9); there is no `LinkView`/`LinkStatsView` DTO in the
 * TUI. None of these functions sanitize for a terminal: every server/stored
 * string (slug, target, tags, metadata, campaign, weekday labels) is sanitized
 * at the render sink by the screen (threat model T-110). All server-derived
 * series lengths and strings are bounded here (threat model T-121): the day
 * series is capped at @ref LinkDetailModel::k_max_buckets entries.
 */

/**
 * @brief One row of the D4 two-column detail grid.
 *
 * A pure value type carrying the (dim) label and the value for each of the two
 * columns. The right column is empty (both strings empty) for a row that spans
 * the full width (e.g. the target URL or metadata row).
 */
struct DetailRow
{
    ///< Left column label (rendered dim), e.g. `"id"`.
    std::string left_label;

    ///< Left column value (raw server data; sanitized by the screen), or `"-"`
    ///< when absent.
    std::string left_value;

    ///< Right column label (rendered dim), or empty for a full-width row.
    std::string right_label;

    ///< Right column value (raw server data; sanitized by the screen), or empty
    ///< for a full-width row.
    std::string right_value;
};

/**
 * @brief Formats an unsigned count with thousands separators, e.g. `84210`
 * becomes `"84,210"`.
 *
 * Pure and locale-independent: the digits are grouped in threes from the right
 * with an ASCII comma. `0` yields `"0"`; values below `1000` are returned
 * unchanged (`999` -> `"999"`, `1000` -> `"1,000"`).
 *
 * @param value The count to format.
 * @return The grouped decimal text; never empty.
 */
std::string format_count(std::uint64_t value);

/**
 * @brief Returns the English three-letter weekday label (`"Mon"`..`"Sun"`) for
 * a UTC instant given as Unix epoch seconds.
 *
 * Uses a pure civil-date algorithm (no `<chrono>` calendar API, no
 * `std::gmtime` which is not thread-safe and locale-sensitive): the weekday
 * depends only on the whole-day count since the epoch, computed with floor
 * division so negative (pre-1970) instants are handled correctly. 1970-01-01
 * (epoch day 0) is a Thursday, which anchors the mapping.
 *
 * @param epoch_seconds The instant as Unix epoch seconds (UTC); may be
 * negative.
 * @return One of `"Mon"`, `"Tue"`, `"Wed"`, `"Thu"`, `"Fri"`, `"Sat"`, `"Sun"`.
 */
std::string weekday_from_epoch(std::int64_t epoch_seconds);

/**
 * @brief Renders a text sparkline: one ramp glyph per value, scaled to the
 * series maximum.
 *
 * Each value maps to a level of a fixed ramp and is emitted as the ramp glyph
 * for that level. The ASCII ramp (`ascii == true`, the mono theme) is
 * `" .:-=+*#"` (8 levels, level 0 is a space); the Unicode ramp is the block
 * elements `"▁".."█"` (8 levels, level 0 is the lowest visible block),
 * which the mono ASCII filter folds anyway. Scaling uses `long double` so a
 * value near `2^63` cannot overflow. Rules:
 *
 * - @p width `0` yields an empty string;
 * - an empty @p values yields an empty string;
 * - at most @p width leading values are rendered (the series is truncated,
 * never padded), so the result is at most @p width glyphs;
 * - when the maximum value is `0` (empty-but-for-zeros series) every glyph is
 *   the level-0 baseline (a flat baseline).
 *
 * @param values The per-column counts.
 * @param width The maximum number of glyphs to emit.
 * @param ascii `true` to use the ASCII ramp, `false` for the Unicode block
 * ramp.
 * @return The sparkline string, at most @p width display glyphs.
 */
std::string sparkline(const std::vector<std::uint64_t>& values,
                      std::size_t width,
                      bool ascii);

/**
 * @brief Parses and presents one link's `link.get` and `link.stats` results for
 * the D4 detail screen (subtask 06.0/13).
 *
 * Built once per open detail with an injected @ref IClock. The two results are
 * applied independently (@ref apply_link / @ref apply_stats and their error
 * counterparts), so a failing stats call never hides the link fields and vice
 * versa (success criterion 2). Every accessor is a deterministic, headless
 * function of the applied JSON and the clock, so the whole grid/series is
 * unit-tested without a terminal (ADR 0003 rule 6).
 */
class LinkDetailModel
{
public:
    /**
     * @brief Upper bound on the number of day buckets @ref day_bars returns, so
     * a hostile/oversized `time_buckets` array cannot inflate rendering
     * (threat model T-121).
     */
    static constexpr std::size_t k_max_buckets = 400;

    /**
     * @brief Seconds in the 7-day stats window (`7 * 24 * 3600`).
     */
    static constexpr std::int64_t k_window_seconds = 7 * 24 * 3600;

    /**
     * @brief Builds the model over a clock.
     *
     * @param clock Time source used only to compute the stats window (`from =
     * now - 7d`, `to = now`); a reference captured for the model's lifetime, so
     * @p clock must outlive this model. Inject a `ManualClock` in tests for a
     * deterministic window.
     */
    explicit LinkDetailModel(const IClock& clock);

    /**
     * @brief Builds the `link.get {slug}` request.
     *
     * @param slug The link slug to fetch; escaped into the `args` object with
     * `url_shortener::jsonString` (contract C9).
     * @param id The correlation id to echo back in the response.
     * @return A `v == 1` request for `link.get` with `{"slug":<escaped>}` args.
     */
    commands::CommandRequest get_request(const std::string& slug,
                                         const std::string& id) const;

    /**
     * @brief Builds the `link.stats {slug, from, to, bucket}` range request for
     * the 7-day window ending now.
     *
     * `from` and `to` are the window's Unix epoch seconds rendered as decimal
     * JSON *strings* (the `link.stats` descriptor types them as string args),
     * with `from = floor(now) - 7d` and `to = floor(now)`. `bucket` is the
     * model's current bucket (@ref set_bucket; `"day"` by default). The `id` is
     * left empty for the caller to assign.
     *
     * @param slug The link slug; escaped into the `args` object.
     * @return A `v == 1` request for `link.stats` with
     * `{"slug":..,"from":"..","to":"..","bucket":".."}` args and an empty id.
     */
    commands::CommandRequest stats_request(const std::string& slug) const;

    /**
     * @brief Applies a successful `link.get` result (the `LinkView` JSON).
     * @param link_json The result payload; retained verbatim and parsed lazily
     * by @ref rows / @ref slug / @ref is_deleted through `json_view`.
     */
    void apply_link(const std::string& link_json);

    /**
     * @brief Applies a successful `link.stats` result (the range report JSON).
     * @param stats_json The result payload; retained verbatim and parsed lazily
     * by @ref stats_header / @ref day_bars.
     */
    void apply_stats(const std::string& stats_json);

    /**
     * @brief Records that the `link.get` call failed.
     * @param code The wire error-code name (e.g. `"not_found"`), used by the
     * screen to decide auto-pop and the error line.
     * @param message The error message; retained for diagnostics.
     */
    void apply_link_error(const std::string& code, const std::string& message);

    /**
     * @brief Records that the `link.stats` call failed.
     * @param code The wire error-code name, shown as `stats unavailable:
     * <code>`.
     * @param message The error message; retained for diagnostics.
     */
    void apply_stats_error(const std::string& code, const std::string& message);

    /**
     * @brief Whether a `link.get` result has been applied successfully.
     * @return `true` after @ref apply_link, `false` before any result or after
     * @ref apply_link_error.
     */
    bool has_link() const noexcept { return m_link_state_ == State::loaded; }

    /**
     * @brief Whether the `link.get` call failed.
     * @return `true` after @ref apply_link_error.
     */
    bool link_failed() const noexcept { return m_link_state_ == State::failed; }

    /**
     * @brief Whether a `link.stats` result has been applied successfully.
     * @return `true` after @ref apply_stats.
     */
    bool has_stats() const noexcept { return m_stats_state_ == State::loaded; }

    /**
     * @brief Whether the `link.stats` call failed.
     * @return `true` after @ref apply_stats_error.
     */
    bool stats_failed() const noexcept
    {
        return m_stats_state_ == State::failed;
    }

    /**
     * @brief The `link.get` error-code name, valid when @ref link_failed.
     * @return The wire code name, or empty when the link did not fail.
     */
    const std::string& link_error_code() const noexcept
    {
        return m_link_error_code_;
    }

    /**
     * @brief The `link.stats` error-code name, valid when @ref stats_failed.
     * @return The wire code name, or empty when stats did not fail.
     */
    const std::string& stats_error_code() const noexcept
    {
        return m_stats_error_code_;
    }

    /**
     * @brief The raw `link.get` JSON, for prefilling the edit form.
     * @return The applied `LinkView` JSON, or empty before @ref apply_link.
     */
    const std::string& link_json() const noexcept { return m_link_json_; }

    /**
     * @brief The decoded `slug` from the applied `link.get` result.
     * @return The slug string, or empty when no link has been applied or it has
     * no `slug` field.
     */
    std::string slug() const;

    /**
     * @brief Whether the applied link's `status` is `"deleted"`.
     * @return `true` when the `link.get` result's `status` is `"deleted"`.
     */
    bool is_deleted() const;

    /**
     * @brief Sets the stats aggregation bucket used by @ref stats_request.
     * @param bucket One of `"hour"`, `"day"`, `"week"`; an empty value is
     * ignored so the request always carries a valid bucket.
     */
    void set_bucket(const std::string& bucket);

    /**
     * @brief The current stats bucket.
     * @return The bucket name (`"day"` by default).
     */
    const std::string& bucket() const noexcept { return m_bucket_; }

    /**
     * @brief The D4 two-column detail rows derived from the `link.get` result.
     *
     * Produces six rows: id / short URL, target (full width), status /
     * redirect, created / expires, tags / campaign, metadata (full width). Each
     * value is the decoded server string (or `"-"` when absent); `expires_at`
     * is not part of the `LinkView` JSON and always renders `"-"` (consistent
     * with the D3 EXPIRES column). The campaign renders as `name / source /
     * medium`. The values are raw server data; the screen sanitizes them before
     * display (T-110).
     *
     * @return The six detail rows; empty when no link has been applied.
     */
    std::vector<DetailRow> rows() const;

    /**
     * @brief The stats header line, e.g.
     * `"Stats (7d)  attempts 84,210  success 83,720  [bucket: day]"`.
     *
     * @return The header built from `total_attempts`, `successful_redirects`
     * and `bucket`; a neutral placeholder when no stats have been applied.
     */
    std::string stats_header() const;

    /**
     * @brief The per-bucket (weekday label, count) series for the stats strip.
     *
     * Each entry pairs the bucket's UTC weekday label (from its `bucket_start`)
     * with its `count`, in chronological (server) order, capped at
     * @ref k_max_buckets entries.
     *
     * @return The day series; empty when no stats have been applied.
     */
    std::vector<std::pair<std::string, std::uint64_t>> day_bars() const;

private:
    /// Load state of one of the two independent requests.
    enum class State
    {
        pending,  ///< No result or error applied yet (the loading skeleton).
        loaded,  ///< A successful result has been applied.
        failed  ///< The call failed; an error code/message is recorded.
    };

    ///< Time source for the stats window; not owned, must outlive this model.
    const IClock& m_clock_;

    ///< Load state of the `link.get` call.
    State m_link_state_ = State::pending;

    ///< Load state of the `link.stats` call.
    State m_stats_state_ = State::pending;

    ///< The applied `link.get` JSON (verbatim); empty until @ref apply_link.
    std::string m_link_json_;

    ///< The applied `link.stats` JSON (verbatim); empty until @ref apply_stats.
    std::string m_stats_json_;

    ///< The `link.get` error-code name when @ref m_link_state_ is failed.
    std::string m_link_error_code_;

    ///< The `link.get` error message when @ref m_link_state_ is failed.
    std::string m_link_error_message_;

    ///< The `link.stats` error-code name when @ref m_stats_state_ is failed.
    std::string m_stats_error_code_;

    ///< The `link.stats` error message when @ref m_stats_state_ is failed.
    std::string m_stats_error_message_;

    ///< The current stats bucket for @ref stats_request; `"day"` by default.
    std::string m_bucket_ = "day";
};

}  // namespace url_shortener::tui
