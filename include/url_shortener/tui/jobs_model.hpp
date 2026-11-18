#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <url_shortener/tui/table_model.hpp>

namespace url_shortener::tui
{

/**
 * @file jobs_model.hpp
 * @brief The FTXUI-free model behind the Jobs screen (D8, subtask 06.0/16):
 * the @ref JobsModel row/selection state over a @ref TableModel, the
 * @ref progress_bar / @ref outcome_text cell formatters, and the
 * @ref PollSchedule backoff policy.
 *
 * Everything here belongs to `url_shortener_tui_model` (ADR 0003 rule 6): no
 * FTXUI, no colour, no threads, no clocks. JSON is read only through the
 * sanctioned `json_view` helpers (contract C9). None of these functions
 * sanitize for a terminal - every server/stored string (a job's command,
 * state, progress, @ref outcome_text) is sanitized at the render sink
 * (`TableView`/`ResultViewerModel`, threat model T-110). All server-derived
 * values are length-bounded here (threat model T-121): the list is capped at
 * @ref JobsModel::k_max_jobs rows and each formatted cell at a fixed byte
 * bound.
 */

/**
 * @brief Renders a textual progress gauge, e.g. `"######----  62%"`.
 *
 * Produces a @p width-column bar of "filled" then "empty" glyphs followed by
 * the percentage. When @p ascii is `true` the glyphs are `#` / `-` (the mono
 * theme); otherwise they are the block elements U+2588 / U+2591, which the mono
 * ASCII filter folds to the same `#` / `-`. Rules:
 *
 * - `total == 0` is "unknown": the bar is all-empty and the percent reads `?`.
 * - `done >= total` clamps to 100%.
 * - `width < 4` returns the percent text only (no bar), so a degenerate column
 *   still shows a number.
 *
 * The result is plain, printable ASCII (or ASCII plus the two block code
 * points); it is still sanitized by the render sink before display.
 *
 * @param done Work units completed.
 * @param total Total work units; `0` means the total is unknown.
 * @param width Bar width in display columns.
 * @param ascii `true` to use `#` / `-`; `false` to use U+2588 / U+2591.
 * @return The gauge string (bar, two spaces, right-justified percent), or the
 * percent text alone when @p width is below 4.
 */
std::string progress_bar(std::uint64_t done,
                         std::uint64_t total,
                         std::size_t width,
                         bool ascii);

/**
 * @brief Derives the OUTCOME cell text for one job object.
 *
 * Reads the job's `state` and summarizes the result:
 * - `succeeded` -> a summary from the job's `result` when present (`"<n>
 * rows"`,
 *   `"<created> created"` or `"<created>/<total> created"`), otherwise the
 *   literal `"succeeded"` (a `job.list` row carries no `result`, carry-forward
 *   05.0);
 * - `failed` -> the job's `error.code` when present, otherwise `"failed"`;
 * - `cancelled` -> `"job_cancelled"`;
 * - `queued` / `running` -> `"-"` (no outcome yet);
 * - any other / missing state -> the raw state, or `"-"` when empty.
 *
 * The returned text is untrusted stored data (a failure `code`/`message` may
 * contain control bytes) and is length-bounded but NOT sanitized here; the
 * OUTCOME cell is sanitized by the `TableView` sink before display (T-110).
 *
 * @param job_json One job object's JSON text (a `job.list` item or a `job.get`
 * result); treated as untrusted and read only through `json_view`.
 * @return The bounded OUTCOME text for the job.
 */
std::string outcome_text(const std::string& job_json);

/**
 * @brief Whether a job state is terminal (no further polling needed).
 *
 * @param state A job state wire name.
 * @return `true` for `succeeded`, `failed` or `cancelled`; `false` for
 * `queued`, `running` or any unrecognized value.
 */
bool is_terminal_state(const std::string& state);

/**
 * @brief The poll cadence and error backoff policy for the Jobs screen (D8,
 * ADR 0003 rule 3).
 *
 * Pure and clock-free: it only computes delays and a "should I poll" predicate;
 * the actual timing is driven by a @ref PollTicker. On a successful poll the
 * delay resets to the base cadence; on a failed poll it doubles up to a cap, so
 * a flaky backend is retried with exponential backoff and recovers
 * automatically.
 */
class PollSchedule
{
public:
    /**
     * @brief Builds a schedule with a base cadence and a backoff ceiling.
     *
     * @param base The normal inter-poll delay (and the delay a success resets
     * to); defaults to 1 s (ADR 0003 rule 3, "jobs are polled on a 1 s timer").
     * @param max_backoff The largest delay a run of failures may back off to;
     * defaults to 5 s. Clamped to be at least @p base.
     */
    explicit PollSchedule(
        std::chrono::milliseconds base = std::chrono::milliseconds(1000),
        std::chrono::milliseconds max_backoff =
            std::chrono::milliseconds(5000));

    /**
     * @brief Advances the backoff state and returns the next poll delay.
     *
     * @param last_call_ok `true` when the last poll succeeded (resets the delay
     * to the base cadence); `false` when it failed (doubles the current delay,
     * capped at the backoff ceiling).
     * @return The delay to wait before the next poll.
     */
    std::chrono::milliseconds next_delay(bool last_call_ok);

    /**
     * @brief Whether the Jobs poll should keep running.
     *
     * @param screen_visible `true` when the Jobs tab is the active screen.
     * @param any_active_job `true` when at least one known job is still
     * `queued`/`running`.
     * @return `true` when the screen is visible OR an active job is known, so a
     * backgrounded Jobs tab keeps polling only while a job is still in flight
     * and otherwise goes idle (no busy loop on another tab).
     */
    bool should_poll(bool screen_visible, bool any_active_job) const;

    /**
     * @brief The current backoff delay (diagnostic/testing accessor).
     * @return The delay @ref next_delay last returned, or the base before any
     * call.
     */
    std::chrono::milliseconds current_delay() const { return m_current_; }

private:
    ///< Base cadence; a successful poll resets @ref m_current_ to this.
    std::chrono::milliseconds m_base_;

    ///< Backoff ceiling; a run of failures never waits longer than this.
    std::chrono::milliseconds m_max_;

    ///< The delay the next poll should wait.
    std::chrono::milliseconds m_current_;
};

/**
 * @brief The row/selection state of the Jobs table (D8), fed by `job.list`.
 *
 * Owns a @ref TableModel (the ID / COMMAND / STATE / PROGRESS / STARTED /
 * FINISHED / OUTCOME columns) and a parallel list of per-row job facts (id and
 * state) so it can answer selection and cancellability without re-parsing. The
 * screen renders the owned @ref TableModel through a `TableView` and drives
 * selection through it; @ref JobsModel reads that selection back.
 *
 * The table is driven directly (no paging query is ever emitted): @ref
 * apply_list replaces the single page in place, so the model never advances the
 * table's query generation and the screen needs no query handler. The row count
 * is capped at @ref k_max_jobs regardless of what the server returns (threat
 * model T-121). Single-threaded: created and mutated only on the UI thread.
 */
class JobsModel
{
public:
    /**
     * @brief Upper bound on rows materialized from one `job.list` result, so a
     * server that over-returns cannot inflate the table (threat model T-121).
     * Matches the screen's `job.list {limit:50}` request.
     */
    static constexpr std::size_t k_max_jobs = 50;

    /**
     * @brief Builds an empty Jobs model.
     *
     * @param ascii_progress `true` to render progress bars with `#` / `-` (the
     * mono theme), `false` to use the block glyphs; passed straight to
     * @ref progress_bar for every row.
     */
    explicit JobsModel(bool ascii_progress = false);

    /**
     * @brief Replaces the table rows from a `job.list` result payload.
     *
     * Reads the result's `items` array (capped at @ref k_max_jobs) and, for
     * each job, builds the seven cells (id, command, state, progress bar,
     * started-or-`-`, finished-or-`-`, @ref outcome_text) plus a fact record
     * (id, state). A malformed payload or a missing/empty `items` array yields
     * an empty table rather than throwing. The previous selection index is
     * preserved where possible (clamped into the new rows).
     *
     * @param job_list_result_json The `job.list` response `result` JSON
     * (an object with `items` and optional `total`); treated as untrusted and
     * read only through `json_view`.
     */
    void apply_list(const std::string& job_list_result_json);

    /**
     * @brief Whether any known job is still running.
     * @return `true` when at least one row's state is `queued` or `running`.
     */
    bool any_active() const;

    /**
     * @brief The id of the currently selected job, if any.
     * @return The selected row's job id, or `std::nullopt` when the table is
     * empty or the id is missing.
     */
    std::optional<std::string> selected_id() const;

    /**
     * @brief Whether the selected job can be cancelled.
     * @return `true` only when a row is selected and its state is `queued` or
     * `running` (a terminal job cannot be cancelled).
     */
    bool can_cancel_selected() const;

    /**
     * @brief The state wire name of the selected job (for a no-op cancel note).
     * @return The selected row's state, or an empty string when nothing is
     * selected.
     */
    std::string selected_state() const;

    /**
     * @brief Moves the selection to the row with id @p id, if present.
     *
     * Used by the Import/Export screen hand-off (`ScreenContext::focus_job_id`)
     * to land on a freshly-started job. A no-op when no row has that id.
     *
     * @param id The job id to select.
     */
    void focus_job(const std::string& id);

    /**
     * @brief The raw JSON of the selected job row (for the detail view).
     * @return A pointer to the selected row's raw job JSON, or `nullptr` when
     * nothing is selected. Valid until the next @ref apply_list.
     */
    const std::string* selected_raw() const;

    /**
     * @brief The number of rows currently materialized.
     * @return The row count (at most @ref k_max_jobs).
     */
    std::size_t job_count() const;

    /**
     * @brief The underlying table for rendering and selection navigation.
     * @return A reference to the owned @ref TableModel; valid for this model's
     * lifetime.
     */
    TableModel& table() { return m_table_; }

    /// @copydoc table()
    const TableModel& table() const { return m_table_; }

    /**
     * @brief The Jobs table column definitions (D8 order).
     * @return ID / COMMAND / STATE / PROGRESS / STARTED / FINISHED / OUTCOME.
     */
    static std::vector<ColumnDef> columns();

private:
    /**
     * @brief One row's selection-relevant facts, parallel to the table rows.
     */
    struct JobFact
    {
        ///< The job id (`j-<n>`); empty when the row had no id.
        std::string id;

        ///< The job state wire name; empty when the row had no state.
        std::string state;
    };

    /**
     * @brief Moves the table selection to row @p index.
     *
     * The @ref TableModel exposes only relative selection moves, so this walks
     * from the first row; bounded by @ref k_max_jobs.
     *
     * @param index Zero-based row index to select.
     */
    void select_index(std::size_t index);

    ///< `true` to render progress bars with `#` / `-` (mono).
    bool m_ascii_;

    ///< The rendered table (columns + rows + selection).
    TableModel m_table_;

    ///< Per-row facts parallel to the table rows, in row order.
    std::vector<JobFact> m_facts_;
};

}  // namespace url_shortener::tui
