/**
 * @file dashboard_model.cpp
 * @brief Implements the FTXUI-free Dashboard model (D2, subtask 06.0/17):
 * request building (one tagged batch per refresh), per-panel response folding
 * through `json_view`, and the derived counters / sample ranking / recent-
 * changes / status lines. See @ref dashboard_model.hpp.
 */
#include <algorithm>
#include <cstdlib>
#include <limits>
#include <string>
#include <utility>

#include "url_shortener/tui/dashboard_model.hpp"

#include <url_shortener/tui/jobs_model.hpp>  // percent_of (reused, 16)
#include <url_shortener/tui/json_view.hpp>

namespace url_shortener::tui
{

namespace
{

/// Full-block glyph U+2588 for a bar in the colour themes (folded to `#` by the
/// mono ASCII filter).
constexpr char k_block[] = "\xe2\x96\x88";

/// Adds @p b to @p a, saturating at `UINT64_MAX` instead of wrapping so a run
/// of up to 200 INT64_MAX-clamped sample counts cannot overflow (T-121).
std::uint64_t saturating_add(std::uint64_t a, std::uint64_t b)
{
    const std::uint64_t max = std::numeric_limits<std::uint64_t>::max();
    return a > max - b ? max : a + b;
}

/// Orders top-slug entries by redirect count descending, breaking ties by slug
/// ascending so the ranking is deterministic regardless of server order.
bool by_count_desc(const TopSlug& a, const TopSlug& b)
{
    if (a.count != b.count) {
        return a.count > b.count;
    }
    return a.slug < b.slug;
}

/// Reads a non-negative integer field as unsigned (negatives / absent -> 0).
std::uint64_t unsigned_field(const std::string& json, const std::string& field)
{
    const std::int64_t value = get_int(json, field).value_or(0);
    return value < 0 ? 0u : static_cast<std::uint64_t>(value);
}

/// Reads the `stats.redirects_24h` of one `link.list` item object (0 when the
/// item has no readable `stats` object or count).
std::uint64_t item_redirects_24h(const std::string& item_json)
{
    const std::optional<std::string> stats = raw_field(item_json, "stats");
    if (!stats.has_value()) {
        return 0;
    }
    return unsigned_field(*stats, "redirects_24h");
}

/// Reads a numeric field as a double, using the sanctioned scanner to extract
/// the raw token (no local JSON tokenizing, contract C9) and converting it with
/// `std::strtod`. Returns `std::nullopt` when the field is absent or its token
/// does not convert cleanly.
std::optional<double> double_field(const std::string& json,
                                   const std::string& field)
{
    const std::optional<std::string> raw = raw_field(json, field);
    if (!raw.has_value() || raw->empty() || raw->front() == '"'
        || *raw == "null")
    {
        return std::nullopt;
    }
    const char* begin = raw->c_str();
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin || *end != '\0') {
        return std::nullopt;  // trailing junk or empty parse
    }
    return value;
}

/// Extracts `HH:MM` from an RFC 3339 timestamp (`...THH:MM:SS...`) verbatim (no
/// time-zone conversion); empty when the string has no readable `T` + `HH:MM`.
std::string hhmm_of(const std::string& timestamp)
{
    const std::string::size_type t = timestamp.find('T');
    if (t == std::string::npos || t + 6 > timestamp.size()) {
        return std::string();
    }
    return timestamp.substr(t + 1, 5);  // "HH:MM"
}

}  // namespace

DashboardModel::DashboardModel(bool has_stats_summary)
    : m_has_stats_summary_(has_stats_summary)
{
}

std::vector<commands::CommandRequest> DashboardModel::refresh_requests(
    const std::function<std::string()>& next_id) const
{
    const auto make =
        [&next_id](const char* command, const char* args, const char* panel)
    {
        commands::CommandRequest request;
        request.v = 1;
        request.id = next_id() + "#" + panel;
        request.command = command;
        request.args_json = args;
        return request;
    };

    std::vector<commands::CommandRequest> requests;
    // Counts: one tiny page per lifecycle filter; only `total` is read.
    requests.push_back(
        make("link.list", "{\"status\":\"all\",\"limit\":1}", "counts.total"));
    requests.push_back(make(
        "link.list", "{\"status\":\"active\",\"limit\":1}", "counts.active"));
    requests.push_back(make("link.list",
                            "{\"status\":\"disabled\",\"limit\":1}",
                            "counts.disabled"));
    requests.push_back(make(
        "link.list", "{\"status\":\"deleted\",\"limit\":1}", "counts.deleted"));
    // Recent changes: newest-updated links.
    requests.push_back(
        make("link.list",
             "{\"status\":\"all\",\"sort\":\"updated_at\",\"order\":\"desc\","
             "\"limit\":5}",
             "recent"));
    // Redirects / Top slugs: exact figures from `stats.summary` when available,
    // otherwise a sample of the most recently updated links.
    if (m_has_stats_summary_) {
        requests.push_back(make("stats.summary", "{}", "redirects"));
    }
    else {
        requests.push_back(make("link.list",
                                "{\"status\":\"all\",\"sort\":\"updated_at\","
                                "\"order\":\"desc\",\"limit\":200}",
                                "redirects"));
    }
    // Running jobs, backend identity and the lifetime click-events count.
    requests.push_back(make("job.list", "{\"state\":\"running\"}", "jobs"));
    requests.push_back(make("system.info", "{}", "backend"));
    requests.push_back(make("db.tables", "{}", "events"));
    return requests;
}

void DashboardModel::start_refresh()
{
    for (SubStatus& status : m_counts_status_) {
        status = SubStatus::pending;
    }
    m_counts_error_.clear();
}

std::string DashboardModel::panel_of(const std::string& request_id)
{
    const std::string::size_type hash = request_id.rfind('#');
    if (hash == std::string::npos) {
        return std::string();
    }
    return request_id.substr(hash + 1);
}

void DashboardModel::apply(const std::string& panel,
                           const commands::CommandResponse& response)
{
    if (panel == "counts.total") {
        apply_counts(counts_total, &m_counts_.total, response);
    }
    else if (panel == "counts.active") {
        apply_counts(counts_active, &m_counts_.active, response);
    }
    else if (panel == "counts.disabled") {
        apply_counts(counts_disabled, &m_counts_.disabled, response);
    }
    else if (panel == "counts.deleted") {
        apply_counts(counts_deleted, &m_counts_.deleted, response);
    }
    else if (panel == "recent") {
        apply_recent(response);
    }
    else if (panel == "redirects") {
        if (m_has_stats_summary_) {
            apply_stats_summary(response);
        }
        else {
            apply_sample(response);
        }
    }
    else if (panel == "jobs") {
        if (response.ok) {
            m_jobs_json_ = response.result_json;
            m_jobs_state_ = PanelState::ready;
        }
        else {
            m_jobs_json_.clear();
            m_jobs_state_ = PanelState::unavailable;
        }
    }
    else if (panel == "backend") {
        if (response.ok) {
            const std::string& json = response.result_json;
            if (const std::optional<std::string> backend =
                    raw_field(json, "backend"))
            {
                m_backend_metadata_ =
                    get_string(*backend, "metadata").value_or(std::string());
                m_backend_cache_ =
                    get_string(*backend, "cache").value_or(std::string());
            }
            m_persistent_ = get_bool(json, "persistent").value_or(true);
            m_backend_state_ = PanelState::ready;
        }
        else {
            m_backend_state_ = PanelState::unavailable;
        }
    }
    else if (panel == "events") {
        // Best-effort: a failure leaves the last-known count untouched and
        // never degrades another panel.
        if (response.ok) {
            const std::optional<std::string> tables =
                raw_field(response.result_json, "tables");
            if (tables.has_value()) {
                for (const std::string& table : split_array(*tables)) {
                    if (get_string(table, "name").value_or(std::string())
                        == "click_events")
                    {
                        m_click_events_ = unsigned_field(table, "rows");
                        break;
                    }
                }
            }
        }
    }
    // An unknown panel is ignored (a stray id routes nowhere).
}

void DashboardModel::apply_counts(CountsIndex slot,
                                  std::uint64_t* field,
                                  const commands::CommandResponse& response)
{
    if (response.ok) {
        *field = unsigned_field(response.result_json, "total");
        m_counts_status_[slot] = SubStatus::ok;
    }
    else {
        m_counts_status_[slot] = SubStatus::failed;
        if (m_counts_error_.empty()) {
            m_counts_error_ = response.error.message;
        }
    }

    bool any_failed = false;
    bool all_ok = true;
    for (const SubStatus status : m_counts_status_) {
        if (status == SubStatus::failed) {
            any_failed = true;
        }
        if (status != SubStatus::ok) {
            all_ok = false;
        }
    }
    if (any_failed) {
        m_counts_.state = PanelState::unavailable;
    }
    else if (all_ok) {
        m_counts_.state = PanelState::ready;
    }
    // Otherwise keep the current state so an auto-refresh never flashes
    // loading.
}

void DashboardModel::apply_sample(const commands::CommandResponse& response)
{
    if (!response.ok) {
        m_redirects_.state = PanelState::unavailable;
        m_redirects_error_ = response.error.message;
        m_redirects_.redirects_24h.reset();
        m_redirects_.success_pct.reset();
        m_redirects_.p50_ms.reset();
        m_top_.clear();
        return;
    }

    const std::optional<std::string> items_raw =
        raw_field(response.result_json, "items");
    std::vector<TopSlug> slugs;
    std::uint64_t sum = 0;
    if (items_raw.has_value()) {
        std::size_t seen = 0;
        for (const std::string& item : split_array(*items_raw)) {
            if (seen >= k_max_sample) {
                break;  // bound server-derived work (T-121)
            }
            ++seen;
            const std::uint64_t count = item_redirects_24h(item);
            sum = saturating_add(sum, count);
            slugs.push_back(
                {get_string(item, "slug").value_or(std::string()), count});
        }
    }
    std::stable_sort(slugs.begin(), slugs.end(), by_count_desc);
    if (slugs.size() > k_max_top) {
        slugs.resize(k_max_top);
    }

    m_redirects_.state = PanelState::ready;
    m_redirects_.sampled = true;
    m_redirects_.redirects_24h = sum;
    m_redirects_.success_pct.reset();
    m_redirects_.p50_ms.reset();
    m_redirects_error_.clear();
    m_top_ = std::move(slugs);
}

void DashboardModel::apply_stats_summary(
    const commands::CommandResponse& response)
{
    if (!response.ok) {
        m_redirects_.state = PanelState::unavailable;
        m_redirects_error_ = response.error.message;
        m_redirects_.redirects_24h.reset();
        m_redirects_.success_pct.reset();
        m_redirects_.p50_ms.reset();
        m_top_.clear();
        return;
    }

    const std::string& json = response.result_json;
    m_redirects_.state = PanelState::ready;
    m_redirects_.sampled = false;
    m_redirects_.redirects_24h = get_int(json, "redirects_24h").has_value()
        ? std::optional<std::uint64_t>(unsigned_field(json, "redirects_24h"))
        : std::nullopt;
    m_redirects_.success_pct = double_field(json, "success_pct");
    m_redirects_.p50_ms = double_field(json, "p50_ms");
    m_redirects_error_.clear();

    std::vector<TopSlug> slugs;
    if (const std::optional<std::string> top = raw_field(json, "top_slugs")) {
        for (const std::string& entry : split_array(*top)) {
            if (slugs.size() >= k_max_sample) {
                break;  // bound server-derived work before ranking (T-121)
            }
            slugs.push_back({get_string(entry, "slug").value_or(std::string()),
                             unsigned_field(entry, "count")});
        }
    }
    // The server need not sort top_slugs; rank here so the bar max (the first
    // entry's count) scales the series correctly.
    std::stable_sort(slugs.begin(), slugs.end(), by_count_desc);
    if (slugs.size() > k_max_top) {
        slugs.resize(k_max_top);
    }
    m_top_ = std::move(slugs);
}

void DashboardModel::apply_recent(const commands::CommandResponse& response)
{
    if (!response.ok) {
        m_recent_state_ = PanelState::unavailable;
        m_recent_error_ = response.error.message;
        m_recent_.clear();
        return;
    }

    std::vector<RecentChange> rows;
    if (const std::optional<std::string> items =
            raw_field(response.result_json, "items"))
    {
        for (const std::string& item : split_array(*items)) {
            if (rows.size() >= k_max_recent) {
                break;  // bound server-derived work (T-121)
            }
            RecentChange row;
            row.time_hhmm =
                hhmm_of(get_string(item, "updated_at").value_or(std::string()));
            row.slug = get_string(item, "slug").value_or(std::string());
            row.status = get_string(item, "status").value_or(std::string());
            rows.push_back(std::move(row));
        }
    }
    m_recent_ = std::move(rows);
    m_recent_state_ = PanelState::ready;
    m_recent_error_.clear();
}

std::string DashboardModel::jobs_line() const
{
    if (m_jobs_state_ == PanelState::loading) {
        return "Jobs: ...";
    }
    if (m_jobs_state_ == PanelState::unavailable) {
        return "Jobs: n/a";
    }

    const std::optional<std::string> items = raw_field(m_jobs_json_, "items");
    const std::vector<std::string> jobs =
        items.has_value() ? split_array(*items) : std::vector<std::string>();
    if (jobs.empty()) {
        return "Jobs: none running";
    }

    const std::string& first = jobs.front();
    const std::string command =
        get_string(first, "command").value_or(std::string());
    std::uint64_t pct = 0;
    if (const std::optional<std::string> progress =
            raw_field(first, "progress"))
    {
        const std::uint64_t done = unsigned_field(*progress, "done");
        const std::uint64_t total = unsigned_field(*progress, "total");
        pct = percent_of(done, total);  // overflow-safe, shared with 16
    }
    return "Jobs: " + std::to_string(jobs.size()) + " running (" + command + " "
        + std::to_string(pct) + "%)";
}

std::string DashboardModel::backend_line() const
{
    if (m_backend_state_ == PanelState::loading) {
        return "Backend: ...";
    }
    if (m_backend_state_ == PanelState::unavailable) {
        return "Backend: n/a";
    }
    const std::string metadata = m_backend_metadata_.empty()
        ? std::string("unknown")
        : m_backend_metadata_;
    const std::string cache =
        m_backend_cache_.empty() ? std::string("none") : m_backend_cache_;
    return "Backend: " + metadata + "  Cache: " + cache;
}

std::string DashboardModel::bars(std::uint64_t value,
                                 std::uint64_t max,
                                 std::size_t width,
                                 bool ascii) const
{
    if (width == 0 || max == 0 || value == 0) {
        return std::string();
    }
    const long double ratio =
        static_cast<long double>(value) / static_cast<long double>(max);
    std::size_t filled = static_cast<std::size_t>(ratio * width + 0.5L);
    if (filled > width) {
        filled = width;
    }
    const std::string glyph = ascii ? std::string("#") : std::string(k_block);
    std::string out;
    out.reserve(filled * glyph.size());
    for (std::size_t i = 0; i < filled; ++i) {
        out += glyph;
    }
    return out;
}

}  // namespace url_shortener::tui
