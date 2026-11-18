/**
 * @file tables_model.cpp
 * @brief Implementation of the Database tables browser view-model (D6, subtask
 * 06.0/14): parsing a `db.tables` result into @ref
 * url_shortener::tui::TableInfo rows, building the `db.rows` @ref
 * url_shortener::commands::CommandRequest for the selected table, and deriving
 * its right-pane @ref url_shortener::tui::ColumnDef set. Pure model code: JSON
 * is read only through `json_view` (contract C9) and no string is sanitized
 * here (the render sinks do that, T-110).
 */
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "url_shortener/tui/tables_model.hpp"

#include <url_shortener/core/utils.hpp>
#include <url_shortener/tui/json_view.hpp>
#include <url_shortener/tui/text_fit.hpp>

namespace url_shortener::tui
{

namespace
{

///< Display width of the left-pane name field (marker + space + name) before
///< the row count is appended; chosen so a typical table name plus count fits a
///< narrow pane (D6 "> links           1,204").
constexpr std::size_t k_name_field_width = 18;

///< Default max width for a generic string column.
constexpr int k_width_generic = 24;

///< Max width for a url-like column.
constexpr int k_width_url = 40;

///< Max width for a timestamp column.
constexpr int k_width_timestamp = 20;

///< Max width for an id-like column.
constexpr int k_width_id = 12;

///< Max width for an integer column.
constexpr int k_width_int = 12;

///< Max width for a boolean column.
constexpr int k_width_bool = 8;

/// Lower-cases an ASCII string for case-insensitive name matching.
std::string to_lower_ascii(const std::string& in)
{
    std::string out = in;
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
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

/// Formats @p value with thousands separators, e.g. `1204` -> `"1,204"`.
std::string group_thousands(std::uint64_t value)
{
    const std::string digits = std::to_string(value);
    std::string out;
    out.reserve(digits.size() + digits.size() / 3);
    const std::size_t first_group =
        digits.size() % 3 == 0 ? 3 : digits.size() % 3;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i >= first_group && (i - first_group) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return out;
}

/// Picks the display max width and right-alignment for a column from its
/// type/name (implementation note: id 12, url 40, ts 20).
void width_for_column(const TableColumnInfo& column,
                      int& max_width,
                      bool& right_align)
{
    const std::string lower_name = to_lower_ascii(column.name);
    const std::string lower_type = to_lower_ascii(column.type);
    right_align = false;

    if (lower_type == "int" || lower_type == "integer"
        || lower_type == "number")
    {
        max_width = k_width_int;
        right_align = true;
        return;
    }
    if (lower_type == "bool" || lower_type == "boolean") {
        max_width = k_width_bool;
        return;
    }
    if (lower_type == "timestamp" || lower_type == "datetime"
        || (lower_name.size() >= 3
            && lower_name.compare(lower_name.size() - 3, 3, "_at") == 0))
    {
        max_width = k_width_timestamp;
        return;
    }
    if (lower_name == "url" || lower_name.find("url") != std::string::npos) {
        max_width = k_width_url;
        return;
    }
    if (lower_name == "id"
        || (lower_name.size() >= 3
            && lower_name.compare(lower_name.size() - 3, 3, "_id") == 0))
    {
        max_width = k_width_id;
        return;
    }
    max_width = k_width_generic;
}

}  // namespace

bool TablesModel::is_secret_column_name(const std::string& name)
{
    if (name == "client_id_hash") {
        return false;  // the one allowed hash column (the HMAC client id)
    }
    const std::string lower = to_lower_ascii(name);
    static const char* const k_secret_markers[] = {
        "hash", "token", "salt", "secret", "password", "dsn"};
    for (const char* marker : k_secret_markers) {
        if (lower.find(marker) != std::string::npos) {
            return true;
        }
    }
    return false;
}

void TablesModel::apply_tables(const std::string& db_tables_result_json)
{
    const std::optional<std::string> tables_raw =
        raw_field(db_tables_result_json, "tables");
    const std::vector<std::string> entries = tables_raw.has_value()
        ? split_array(*tables_raw)
        : std::vector<std::string>();

    std::vector<TableInfo> parsed;
    const std::size_t table_count = std::min(entries.size(), k_max_tables);
    parsed.reserve(table_count);

    for (std::size_t i = 0; i < table_count; ++i) {
        const std::string& entry = entries[i];
        TableInfo info;
        info.name = get_string(entry, "name").value_or(std::string());
        info.rows = read_u64(entry, "rows");
        info.writable = get_bool(entry, "writable").value_or(false);

        if (const std::optional<std::string> cols_raw =
                raw_field(entry, "columns"))
        {
            const std::vector<std::string> cols = split_array(*cols_raw);
            const std::size_t col_count = std::min(cols.size(), k_max_columns);
            info.columns.reserve(col_count);
            for (std::size_t c = 0; c < col_count; ++c) {
                TableColumnInfo column;
                column.name =
                    get_string(cols[c], "name").value_or(std::string());
                column.type =
                    get_string(cols[c], "type").value_or(std::string());
                column.sortable = get_bool(cols[c], "sortable").value_or(false);
                column.searchable =
                    get_bool(cols[c], "searchable").value_or(false);
                info.columns.push_back(std::move(column));
            }
        }
        parsed.push_back(std::move(info));
    }

    m_tables_ = std::move(parsed);

    if (m_tables_.empty()) {
        m_selected_.reset();
    }
    else if (!m_selected_.has_value() || *m_selected_ >= m_tables_.size()) {
        m_selected_ = 0;
    }
}

const std::vector<TableInfo>& TablesModel::tables() const
{
    return m_tables_;
}

std::optional<std::size_t> TablesModel::selected() const
{
    return m_selected_;
}

void TablesModel::select(std::size_t index)
{
    if (m_tables_.empty()) {
        m_selected_.reset();
        return;
    }
    m_selected_ = std::min(index, m_tables_.size() - 1);
}

commands::CommandRequest TablesModel::rows_request(
    const PageQuery& query, const std::string& request_id) const
{
    std::string table;
    if (m_selected_.has_value() && *m_selected_ < m_tables_.size()) {
        table = m_tables_[*m_selected_].name;
    }

    std::string args = "{\"table\":" + url_shortener::jsonString(table);
    if (!query.filter.empty()) {
        args += ",\"q\":" + url_shortener::jsonString(query.filter);
    }
    if (!query.sort_key.empty()) {
        args += ",\"sort\":" + url_shortener::jsonString(query.sort_key);
        args += ",\"order\":";
        args += query.descending ? "\"desc\"" : "\"asc\"";
    }
    args += ",\"offset\":" + std::to_string(query.offset);
    args += ",\"limit\":" + std::to_string(query.limit);
    args += "}";

    commands::CommandRequest request;
    request.v = 1;
    request.id = request_id;
    request.command = "db.rows";
    request.args_json = std::move(args);
    return request;
}

std::vector<ColumnDef> TablesModel::columns_for_selected() const
{
    std::vector<ColumnDef> columns;
    if (!m_selected_.has_value() || *m_selected_ >= m_tables_.size()) {
        return columns;
    }
    const std::vector<TableColumnInfo>& source =
        m_tables_[*m_selected_].columns;
    for (const TableColumnInfo& column : source) {
        if (is_secret_column_name(column.name)) {
            continue;  // defence in depth (step 5): never render a secret
                       // column
        }
        if (columns.size() >= k_max_columns) {
            break;
        }
        ColumnDef def;
        def.key = column.name;
        def.title = column.name;
        def.min_width = 4;
        width_for_column(column, def.max_width, def.right_align);
        def.sortable = column.sortable;
        def.visible = true;
        columns.push_back(std::move(def));
    }
    return columns;
}

std::vector<std::string> TablesModel::column_keys_for_selected() const
{
    std::vector<std::string> keys;
    for (const ColumnDef& column : columns_for_selected()) {
        keys.push_back(column.key);
    }
    return keys;
}

bool TablesModel::can_insert() const
{
    if (!m_selected_.has_value() || *m_selected_ >= m_tables_.size()) {
        return false;
    }
    return m_tables_[*m_selected_].writable;
}

std::size_t TablesModel::hidden_secret_columns() const
{
    if (!m_selected_.has_value() || *m_selected_ >= m_tables_.size()) {
        return 0;
    }
    std::size_t hidden = 0;
    for (const TableColumnInfo& column : m_tables_[*m_selected_].columns) {
        if (is_secret_column_name(column.name)) {
            ++hidden;
        }
    }
    return hidden;
}

std::string TablesModel::left_pane_row(std::size_t index) const
{
    if (index >= m_tables_.size()) {
        return std::string();
    }
    const TableInfo& info = m_tables_[index];
    const bool is_selected = m_selected_.has_value() && *m_selected_ == index;
    const std::string prefix =
        std::string(is_selected ? ">" : " ") + " " + info.name;
    return pad_to_width(prefix, k_name_field_width)
        + group_thousands(info.rows);
}

std::string TablesModel::note_text()
{
    return "read-only logical views over storage ports; no auth/secret tables";
}

}  // namespace url_shortener::tui
