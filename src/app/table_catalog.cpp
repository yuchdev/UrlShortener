/**
 * @file table_catalog.cpp
 * @brief The `db.*` table allowlist (@ref url_shortener::app::TableCatalog) and
 * the two result serializers (subtask 15). Name validation is done by hand (no
 * `<regex>`); no SQL or physical table name appears here (milestone contract
 * C6, threat model T-51).
 */
#include <url_shortener/app/table_source.hpp>

#include <array>
#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <url_shortener/core/utils.hpp>

namespace url_shortener
{
namespace app
{

namespace
{

///< Inclusive upper bound, in bytes, on a registered table name. A name longer
///< than this is malformed and rejected by @ref TableCatalog::add. Generous
///< relative to the real table names so it never rejects a legitimate source.
constexpr std::size_t k_table_name_max_length = 64;

///< The auth tables that must never be browsable through `db.*`, compared
///< case-insensitively against a candidate name. These back credentials,
///< sessions, service clients and the audit log; admitting any of them would
///< turn `db.rows` into a credential/audit exfiltration vector (threat model
///< T-51). `user_credentials` is the fifth name alongside the four SQL tables
///< in `005_auth_tables.sql`.
constexpr std::array<std::string_view, 5> k_forbidden_names = {
    "app_users",
    "auth_sessions",
    "service_clients",
    "auth_audit_log",
    "user_credentials",
};

/**
 * @brief Lowercases an ASCII string for case-insensitive forbidden-name
 * matching.
 * @param value Candidate name; only ASCII `A`..`Z` are folded.
 * @return @p value with ASCII uppercase letters lowercased.
 */
std::string to_ascii_lower(std::string_view value)
{
    std::string out(value);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

/**
 * @brief Tests whether @p name is one of the forbidden auth-table names
 * (case-insensitively).
 * @param name Candidate table name.
 * @return `true` when @p name folds to a forbidden name; `false` otherwise.
 */
bool is_forbidden_name(std::string_view name)
{
    const std::string lowered = to_ascii_lower(name);
    for (const std::string_view forbidden : k_forbidden_names) {
        if (lowered == forbidden) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Validates a table name against the `[a-z][a-z0-9_]*` charset and the
 * length cap, by hand (no `<regex>`).
 * @param name Candidate table name.
 * @return `true` when @p name is non-empty, at most @ref
 * k_table_name_max_length bytes, begins with a lowercase ASCII letter, and
 * otherwise contains only lowercase ASCII letters, digits and underscores;
 * `false` otherwise.
 */
bool is_valid_table_name(std::string_view name)
{
    if (name.empty() || name.size() > k_table_name_max_length) {
        return false;
    }
    if (name[0] < 'a' || name[0] > 'z') {
        return false;
    }
    for (const char c : name) {
        const bool is_lower = (c >= 'a' && c <= 'z');
        const bool is_digit = (c >= '0' && c <= '9');
        const bool is_underscore = (c == '_');
        if (!is_lower && !is_digit && !is_underscore) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Serializes one column's `{name,type,sortable,searchable}` object for
 * `db.tables`.
 * @param os Stream to append to.
 * @param column Column to serialize; `name`/`type` are `jsonString`-escaped.
 */
void append_table_column(std::ostringstream& os, const TableColumn& column)
{
    os << "{\"name\":" << jsonString(column.name)
       << ",\"type\":" << jsonString(column.type)
       << ",\"sortable\":" << (column.sortable ? "true" : "false")
       << ",\"searchable\":" << (column.searchable ? "true" : "false") << '}';
}

}  // namespace

bool TableCatalog::add(std::shared_ptr<ITableSource> source)
{
    if (!source) {
        return false;
    }
    const std::string& name = source->name();
    // Forbidden names are checked first, case-insensitively, so a forbidden
    // auth table is refused as forbidden regardless of letter case rather than
    // slipping through on the charset rule.
    if (is_forbidden_name(name)) {
        return false;
    }
    if (!is_valid_table_name(name)) {
        return false;
    }
    if (find(name) != nullptr) {
        return false;
    }
    m_sources_.push_back(std::move(source));
    return true;
}

const ITableSource* TableCatalog::find(std::string_view name) const
{
    for (const std::shared_ptr<ITableSource>& source : m_sources_) {
        if (source->name() == name) {
            return source.get();
        }
    }
    return nullptr;
}

const std::vector<std::shared_ptr<ITableSource>>& TableCatalog::all() const
{
    return m_sources_;
}

std::string serializeTablesJson(const TableCatalog& catalog,
                                const std::vector<std::size_t>& row_counts)
{
    const std::vector<std::shared_ptr<ITableSource>>& sources = catalog.all();
    std::ostringstream os;
    os << "{\"tables\":[";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        if (i != 0) {
            os << ',';
        }
        const ITableSource& source = *sources[i];
        const std::size_t rows = (i < row_counts.size()) ? row_counts[i] : 0;
        os << "{\"name\":" << jsonString(source.name()) << ",\"rows\":" << rows
           << ",\"writable\":" << (source.writable() ? "true" : "false")
           << ",\"columns\":[";
        const std::vector<TableColumn>& columns = source.columns();
        for (std::size_t c = 0; c < columns.size(); ++c) {
            if (c != 0) {
                os << ',';
            }
            append_table_column(os, columns[c]);
        }
        os << "]}";
    }
    os << "]}";
    return os.str();
}

std::string serializeTableRowsJson(const ITableSource& source,
                                   const TableRowsPage& page,
                                   std::size_t offset,
                                   std::size_t limit)
{
    std::ostringstream os;
    os << "{\"table\":" << jsonString(source.name()) << ",\"columns\":[";
    const std::vector<TableColumn>& columns = source.columns();
    for (std::size_t c = 0; c < columns.size(); ++c) {
        if (c != 0) {
            os << ',';
        }
        os << "{\"name\":" << jsonString(columns[c].name)
           << ",\"type\":" << jsonString(columns[c].type) << '}';
    }
    os << "],\"rows\":[";
    for (std::size_t r = 0; r < page.rows_json.size(); ++r) {
        if (r != 0) {
            os << ',';
        }
        // Each element is a complete JSON object already escaped by the source;
        // it is embedded verbatim so the handler never concatenates untrusted
        // text (threat model T-51/T-52).
        os << page.rows_json[r];
    }
    os << "],\"offset\":" << offset << ",\"limit\":" << limit
       << ",\"total\":" << page.total << '}';
    return os.str();
}

}  // namespace app
}  // namespace url_shortener
