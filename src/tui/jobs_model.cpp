/**
 * @file jobs_model.cpp
 * @brief Implementation of the Jobs view-model (D8, subtask 06.0/16): the
 * @ref url_shortener::tui::progress_bar / @ref url_shortener::tui::outcome_text
 * cell formatters, the @ref url_shortener::tui::PollSchedule backoff policy,
 * and the @ref url_shortener::tui::JobsModel row/selection state over a
 * @ref url_shortener::tui::TableModel. Pure model code: JSON is read only
 * through `json_view` (contract C9) and no string is sanitized here (the render
 * sinks do that, T-110).
 */
#include <algorithm>
#include <limits>
#include <string>
#include <utility>

#include "url_shortener/tui/jobs_model.hpp"

#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

/// Byte cap applied to every server-derived cell before it is stored, so a
/// hostile job field cannot inflate memory across @ref JobsModel::k_max_jobs
/// rows (threat model T-121); the render sink width-fits it further.
constexpr std::size_t k_cell_max_bytes = 256;

/// The generation the Jobs table always lives at: it never emits a paging
/// query, so its generation stays 0 and @ref TableModel::apply_result always
/// accepts a direct page.
constexpr std::uint64_t k_direct_generation = 0;

/// Truncate @p s to at most @p max bytes (UTF-8 may be split; the render sink
/// repairs it to U+FFFD). Returns @p s unchanged when already within bound.
std::string cap_bytes(std::string s, std::size_t max = k_cell_max_bytes)
{
    if (s.size() > max) {
        s.resize(max);
    }
    return s;
}

/// Reads a non-negative integer field as `std::uint64_t`; a missing, negative
/// or non-integer value becomes 0.
std::uint64_t read_u64(const std::string& json, const std::string& field)
{
    const std::optional<std::int64_t> v = get_int(json, field);
    if (!v.has_value() || *v < 0) {
        return 0;
    }
    return static_cast<std::uint64_t>(*v);
}

/// Left-pads an ASCII string with spaces to at least @p width bytes.
std::string pad_left_ascii(const std::string& s, std::size_t width)
{
    if (s.size() >= width) {
        return s;
    }
    return std::string(width - s.size(), ' ') + s;
}

/// The glyphs used for a filled / empty gauge cell.
constexpr const char* k_block_filled = "\xE2\x96\x88";  // U+2588 FULL BLOCK
constexpr const char* k_block_empty = "\xE2\x96\x91";  // U+2591 LIGHT SHADE

}  // namespace

std::uint64_t percent_of(std::uint64_t done, std::uint64_t total)
{
    // `done * 100` wraps a uint64 once `done > UINT64_MAX / 100` (~1.84e17).
    // For such extreme counters (done < total here) scale the denominator down
    // first and clamp to 99, since done < total means the percent is strictly
    // below 100 (threat model T-121: server-derived counters are untrusted).
    if (total == 0) {
        return 0;  // no progress is knowable
    }
    if (done >= total) {
        return 100;
    }
    if (done <= std::numeric_limits<std::uint64_t>::max() / 100) {
        return (done * 100) / total;
    }
    const std::uint64_t denom = total / 100;  // > 0 in this branch
    std::uint64_t pct = denom == 0 ? 99 : done / denom;
    if (pct > 99) {
        pct = 99;
    }
    return pct;
}

std::string progress_bar(std::uint64_t done,
                         std::uint64_t total,
                         std::size_t width,
                         bool ascii)
{
    const char* filled = ascii ? "#" : k_block_filled;
    const char* empty = ascii ? "-" : k_block_empty;

    std::string percent;
    std::size_t filled_cells = 0;
    if (total == 0) {
        percent = "?";  // unknown total
    }
    else {
        const std::uint64_t pct = percent_of(done, total);
        percent = std::to_string(pct) + "%";
        filled_cells = static_cast<std::size_t>((pct * width) / 100);
        if (filled_cells > width) {
            filled_cells = width;
        }
    }

    if (width < 4) {
        return percent;  // percent only for a degenerate column
    }

    std::string bar;
    bar.reserve(width * 3 + 8);
    for (std::size_t i = 0; i < width; ++i) {
        bar += (i < filled_cells) ? filled : empty;
    }
    bar += "  ";
    bar += pad_left_ascii(percent, 3);
    return bar;
}

std::string outcome_text(const std::string& job_json)
{
    const std::string state = get_string(job_json, "state").value_or("");

    std::string out;
    if (state == "succeeded") {
        const std::optional<std::string> result = raw_field(job_json, "result");
        if (result.has_value()) {
            if (const std::optional<std::int64_t> rows =
                    get_int(*result, "rows"))
            {
                out = std::to_string(*rows) + " rows";
            }
            else if (const std::optional<std::int64_t> created =
                         get_int(*result, "created"))
            {
                if (const std::optional<std::int64_t> t =
                        get_int(*result, "total"))
                {
                    out = std::to_string(*created) + "/" + std::to_string(*t)
                        + " created";
                }
                else {
                    out = std::to_string(*created) + " created";
                }
            }
            else {
                out = "succeeded";
            }
        }
        else {
            out = "succeeded";
        }
    }
    else if (state == "failed") {
        const std::optional<std::string> error = raw_field(job_json, "error");
        const std::optional<std::string> code =
            error.has_value() ? get_string(*error, "code") : std::nullopt;
        out = code.value_or("failed");
    }
    else if (state == "cancelled") {
        out = "job_cancelled";
    }
    else if (state == "queued" || state == "running") {
        out = "-";
    }
    else {
        out = state.empty() ? "-" : state;
    }
    return cap_bytes(std::move(out));
}

bool is_terminal_state(const std::string& state)
{
    return state == "succeeded" || state == "failed" || state == "cancelled";
}

// ---------------------------------------------------------------------------
// PollSchedule
// ---------------------------------------------------------------------------

PollSchedule::PollSchedule(std::chrono::milliseconds base,
                           std::chrono::milliseconds max_backoff)
    : m_base_(base)
    , m_max_(std::max(base, max_backoff))
    , m_current_(base)
{
}

std::chrono::milliseconds PollSchedule::next_delay(bool last_call_ok)
{
    if (last_call_ok) {
        m_current_ = m_base_;
        return m_current_;
    }
    const std::chrono::milliseconds doubled = m_current_ * 2;
    m_current_ = doubled > m_max_ ? m_max_ : doubled;
    return m_current_;
}

bool PollSchedule::should_poll(bool screen_visible, bool any_active_job) const
{
    return screen_visible || any_active_job;
}

// ---------------------------------------------------------------------------
// JobsModel
// ---------------------------------------------------------------------------

std::vector<ColumnDef> JobsModel::columns()
{
    std::vector<ColumnDef> cols;
    cols.push_back(ColumnDef {"id", "ID", 4, 10, false, true, false});
    cols.push_back(ColumnDef {"command", "COMMAND", 6, 14, false, true, false});
    cols.push_back(ColumnDef {"state", "STATE", 6, 10, false, true, false});
    cols.push_back(
        ColumnDef {"progress", "PROGRESS", 15, 16, false, true, false});
    cols.push_back(ColumnDef {"started", "STARTED", 8, 19, false, true, false});
    cols.push_back(
        ColumnDef {"finished", "FINISHED", 8, 19, false, true, false});
    cols.push_back(ColumnDef {"outcome", "OUTCOME", 8, 24, false, true, false});
    return cols;
}

JobsModel::JobsModel(bool ascii_progress)
    : m_ascii_(ascii_progress)
    , m_table_(columns(), k_max_jobs)
{
}

void JobsModel::apply_list(const std::string& job_list_result_json)
{
    const std::optional<std::string> items_raw =
        raw_field(job_list_result_json, "items");
    // `split_array` materializes every element, but it runs over the sanctioned
    // scanner whose input is capped at 8 MiB (json_view, threat model T-121),
    // so the element vector is already byte-bounded; the rendered rows are then
    // further capped at k_max_jobs below. A dedicated early-stopping split is
    // intentionally not added here to avoid changing the shared json_view API.
    const std::vector<std::string> jobs = items_raw.has_value()
        ? split_array(*items_raw)
        : std::vector<std::string>();

    PageResult page;
    std::vector<JobFact> facts;
    const std::size_t count = std::min(jobs.size(), k_max_jobs);
    page.rows.reserve(count);
    page.raw_rows.reserve(count);
    facts.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::string& job = jobs[i];
        const std::string id = cap_bytes(get_string(job, "id").value_or(""));
        const std::string command =
            cap_bytes(get_string(job, "command").value_or(""));
        const std::string state =
            cap_bytes(get_string(job, "state").value_or(""));

        std::uint64_t done = 0;
        std::uint64_t total = 0;
        if (const std::optional<std::string> progress =
                raw_field(job, "progress"))
        {
            done = read_u64(*progress, "done");
            total = read_u64(*progress, "total");
        }

        const std::string started = get_string(job, "started_at").value_or("");
        const std::string finished =
            get_string(job, "finished_at").value_or("");

        std::vector<std::string> cells;
        cells.reserve(JobsModel::columns().size());
        cells.push_back(id);
        cells.push_back(command);
        cells.push_back(state);
        cells.push_back(progress_bar(done, total, 10, m_ascii_));
        cells.push_back(started.empty() ? std::string("-")
                                        : cap_bytes(started));
        cells.push_back(finished.empty() ? std::string("-")
                                         : cap_bytes(finished));
        cells.push_back(outcome_text(job));

        page.rows.push_back(std::move(cells));
        page.raw_rows.push_back(cap_bytes(job, k_cell_max_bytes * 16));
        facts.push_back(JobFact {id, state});
    }
    // The server's reported total is ignored for paging: the Jobs view shows a
    // single polled page, so total mirrors the rows shown and page_count is 1.
    page.total = page.rows.size();

    if (m_table_.apply_result(k_direct_generation, std::move(page))) {
        m_facts_ = std::move(facts);
    }
}

bool JobsModel::any_active() const
{
    for (const JobFact& fact : m_facts_) {
        if (fact.state == "queued" || fact.state == "running") {
            return true;
        }
    }
    return false;
}

std::optional<std::string> JobsModel::selected_id() const
{
    const std::optional<std::size_t> sel = m_table_.selected();
    if (!sel.has_value() || *sel >= m_facts_.size()) {
        return std::nullopt;
    }
    const std::string& id = m_facts_[*sel].id;
    if (id.empty()) {
        return std::nullopt;
    }
    return id;
}

bool JobsModel::can_cancel_selected() const
{
    const std::optional<std::size_t> sel = m_table_.selected();
    if (!sel.has_value() || *sel >= m_facts_.size()) {
        return false;
    }
    const std::string& state = m_facts_[*sel].state;
    return state == "queued" || state == "running";
}

std::string JobsModel::selected_state() const
{
    const std::optional<std::size_t> sel = m_table_.selected();
    if (!sel.has_value() || *sel >= m_facts_.size()) {
        return std::string();
    }
    return m_facts_[*sel].state;
}

void JobsModel::focus_job(const std::string& id)
{
    for (std::size_t i = 0; i < m_facts_.size(); ++i) {
        if (m_facts_[i].id == id) {
            select_index(i);
            return;
        }
    }
}

const std::string* JobsModel::selected_raw() const
{
    return m_table_.selected_raw();
}

std::size_t JobsModel::job_count() const
{
    return m_facts_.size();
}

void JobsModel::select_index(std::size_t index)
{
    m_table_.select_first();
    for (std::size_t i = 0; i < index; ++i) {
        m_table_.select_next();
    }
}

}  // namespace url_shortener::tui
