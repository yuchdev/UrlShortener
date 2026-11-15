/**
 * @file click_events_table_source.cpp
 * @brief The `click_events` logical `db.*` table over @ref
 * url_shortener::analytics::IClickEventRepository::ListEvents (subtask 16). The
 * column list is a literal constant here (contract C6) exposing only the HMAC
 * `client_id_hash` as a client identifier; no SQL, SOCI or driver header
 * appears. Repository failures collapse to a fixed, value-free detail so no
 * backend/driver text is ever forwarded (threat model T-52, CWE-209).
 */
#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>
#include <utility>

#include <url_shortener/analytics/analytics_error.hpp>
#include <url_shortener/analytics/click_event.hpp>
#include <url_shortener/analytics/click_event_query.hpp>
#include <url_shortener/app/click_events_table_source.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener
{
namespace app
{

namespace
{

///< Fixed detail written for every repository failure. Constant and value-free
///< so a backend `AnalyticsError::message` (which may contain driver text) can
///< never be forwarded through this source (threat model T-52, CWE-209).
constexpr char k_storage_failure_detail[] = "click event listing failed";

/**
 * @brief Builds one @ref TableColumn.
 * @param name Column name (also the row-object key).
 * @param type Catalog type token (`"string"`, `"int"`, `"timestamp"`).
 * @param sortable Whether `db.rows` may order by this column.
 * @param searchable Whether this column participates in the `q` filter.
 * @return The populated column.
 */
TableColumn make_column(std::string name,
                        std::string type,
                        bool sortable,
                        bool searchable)
{
    TableColumn column;
    column.name = std::move(name);
    column.type = std::move(type);
    column.sortable = sortable;
    column.searchable = searchable;
    return column;
}

/**
 * @brief Maps a pre-whitelisted `db.rows` sort token to an
 * @ref analytics::ClickEventSort.
 *
 * The handler has already rejected any token that is not one of this source's
 * sortable column names, so only `occurred_at`, `slug` and `status_code` reach
 * here; the mapping is nevertheless exhaustive with a safe `occurred_at`
 * default so an unexpected value can never fall through to an unintended column
 * (threat model T-51).
 *
 * @param sort The admissible sort column name.
 * @return The matching sort field, or `occurred_at` as the safe default.
 */
analytics::ClickEventSort to_click_sort(const std::string& sort)
{
    if (sort == "slug") {
        return analytics::ClickEventSort::slug;
    }
    if (sort == "status_code") {
        return analytics::ClickEventSort::status_code;
    }
    return analytics::ClickEventSort::occurred_at;
}

/**
 * @brief Strips the query string and fragment from a referrer value.
 *
 * The referrer is untrusted and can carry sensitive credentials in its query
 * string or fragment (e.g. `?token=`, `#session=`); only the scheme/host/path
 * portion is browse-worthy. Everything from the first `?` or `#` (whichever
 * comes first) is dropped before the value is escaped and emitted (threat model
 * T-52).
 *
 * @param referrer Sanitized referrer value (already control-stripped and
 * length-bounded upstream).
 * @return @p referrer truncated before its first `?` or `#`, or unchanged when
 * it contains neither.
 */
std::string strip_referrer_query(const std::string& referrer)
{
    const std::size_t cut = referrer.find_first_of("?#");
    if (cut == std::string::npos) {
        return referrer;
    }
    return referrer.substr(0, cut);
}

/**
 * @brief Serializes one click event as the flat `click_events` row object.
 *
 * Keys equal the fixed column names; every string cell is `jsonString`-escaped
 * and `referrer` has its query/fragment stripped first. The only client
 * identifier emitted is `client_id_hash` (contract C6). `status_code` is a bare
 * integer.
 *
 * @param event The click event to serialize.
 * @return The single-line JSON object for this row.
 */
std::string serialize_click_row(const analytics::ClickEvent& event)
{
    std::ostringstream os;
    os << '{';
    os << "\"event_id\":" << jsonString(event.event_id);
    os << ",\"occurred_at\":" << jsonString(formatTimestamp(event.occurred_at));
    os << ",\"slug\":" << jsonString(event.slug);
    os << ",\"link_id\":" << jsonString(event.link_id);
    os << ",\"domain\":" << jsonString(event.domain);
    os << ",\"status_code\":" << event.status_code;
    os << ",\"referrer\":" << jsonString(strip_referrer_query(event.referrer));
    os << ",\"user_agent\":" << jsonString(event.user_agent);
    os << ",\"client_id_hash\":" << jsonString(event.client_id_hash);
    os << '}';
    return os.str();
}

}  // namespace

ClickEventsTableSource::ClickEventsTableSource(
    analytics::IClickEventRepository& repository)
    : m_repository_(repository)
{
    // Literal, fixed column list (contract C6): client_id_hash is the only
    // client identifier; no ip/address/raw-id/fingerprint column exists.
    m_columns_ = {
        make_column("event_id", "string", false, false),
        make_column("occurred_at", "timestamp", true, false),
        make_column("slug", "string", true, true),
        make_column("link_id", "string", false, false),
        make_column("domain", "string", false, true),
        make_column("status_code", "int", true, false),
        make_column("referrer", "string", false, false),
        make_column("user_agent", "string", false, false),
        make_column("client_id_hash", "string", false, false),
    };
}

const std::string& ClickEventsTableSource::name() const
{
    static const std::string k_name = "click_events";
    return k_name;
}

bool ClickEventsTableSource::writable() const
{
    return false;
}

const std::vector<TableColumn>& ClickEventsTableSource::columns() const
{
    return m_columns_;
}

Result<std::size_t> ClickEventsTableSource::row_count() const
{
    analytics::ClickEventQuery query;
    query.limit = 0;  // We only need the total, not a materialized window.

    analytics::ClickEventPage page;
    Result<std::size_t> result;
    if (!m_repository_.ListEvents(query, &page)) {
        result.error.code = AppErrorCode::storage_failure;
        result.error.detail = k_storage_failure_detail;
        return result;
    }
    result.value = static_cast<std::size_t>(page.total);
    return result;
}

Result<TableRowsPage> ClickEventsTableSource::rows(
    const TableQuery& query) const
{
    analytics::ClickEventQuery event_query;
    event_query.text = query.q;
    event_query.offset = query.offset;
    // Defense in depth: the handler already bounds limit to 1..500, and the
    // repository re-clamps, but clamp here too so this source never asks for
    // more than the port's own page cap.
    event_query.limit =
        std::min(query.limit, analytics::kMaxClickEventPageSize);
    if (query.sort.has_value()) {
        event_query.sort_by = to_click_sort(*query.sort);
        event_query.descending = query.descending;
    }

    analytics::ClickEventPage page;
    Result<TableRowsPage> result;
    if (!m_repository_.ListEvents(event_query, &page)) {
        result.error.code = AppErrorCode::storage_failure;
        result.error.detail = k_storage_failure_detail;
        return result;
    }

    TableRowsPage rows_page;
    rows_page.total = static_cast<std::size_t>(page.total);
    rows_page.rows_json.reserve(page.items.size());
    for (const analytics::ClickEvent& event : page.items) {
        rows_page.rows_json.push_back(serialize_click_row(event));
    }
    result.value = std::move(rows_page);
    return result;
}

}  // namespace app
}  // namespace url_shortener
