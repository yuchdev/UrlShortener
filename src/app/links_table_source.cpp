/**
 * @file links_table_source.cpp
 * @brief The `links` logical `db.*` table over @ref
 * url_shortener::app::ILinkLister (subtask 16). The column list is a literal
 * constant here (contract C6); no SQL, SOCI or driver header appears, and rows
 * are flat scalar objects whose every cell is `jsonString`-escaped.
 */
#include <sstream>
#include <string>
#include <utility>

#include <url_shortener/app/links_table_source.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener
{
namespace app
{

namespace
{

/**
 * @brief Builds one @ref TableColumn.
 * @param name Column name (also the row-object key).
 * @param type Catalog type token (`"string"`, `"bool"`, `"timestamp"`).
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
 * @brief Maps a pre-whitelisted `db.rows` sort token to a @ref list_sort.
 *
 * The handler has already rejected any token that is not one of this source's
 * sortable column names, so only `slug`, `created_at` and `updated_at` reach
 * here; the mapping is nevertheless exhaustive with a safe `created_at` default
 * so an unexpected value can never fall through to an unintended column (threat
 * model T-51).
 *
 * @param sort The admissible sort column name.
 * @return The matching @ref list_sort, or `list_sort::created_at` as the safe
 * default.
 */
list_sort to_list_sort(const std::string& sort)
{
    if (sort == "slug") {
        return list_sort::slug;
    }
    if (sort == "updated_at") {
        return list_sort::updated_at;
    }
    return list_sort::created_at;
}

/**
 * @brief Emits a `"key":value` pair for an optional timestamp string, writing a
 * JSON `null` when the value is absent.
 * @param os Stream to append to.
 * @param key Already-literal column name (a safe, fixed identifier).
 * @param value Optional RFC 3339 timestamp; `null` is emitted when unset.
 */
void append_optional_timestamp(std::ostringstream& os,
                               const char* key,
                               const std::optional<std::string>& value)
{
    os << '"' << key << "\":";
    if (value.has_value()) {
        os << jsonString(*value);
    }
    else {
        os << "null";
    }
}

/**
 * @brief Serializes one link view as the flat `links` row object.
 *
 * Keys equal the fixed column names and every string cell is `jsonString`-
 * escaped. Only scalar columns are emitted: no `tags`, `metadata`, `campaign`
 * or `short_url` (contract C6). The source does not re-bound `url`
 * (`target_url`) length; it is already bounded upstream at link creation and
 * emitted verbatim after escaping.
 *
 * @param view The link projection to serialize.
 * @return The single-line JSON object for this row.
 */
std::string serialize_link_row(const LinkView& view)
{
    std::ostringstream os;
    os << '{';
    os << "\"id\":" << jsonString(view.id);
    os << ",\"slug\":" << jsonString(view.slug);
    os << ",\"url\":" << jsonString(view.target_url);
    os << ",\"status\":" << jsonString(linkStatusToString(view.status));
    os << ",\"enabled\":" << (view.enabled ? "true" : "false");
    os << ",\"redirect_type\":"
       << jsonString(redirectTypeToString(view.redirect_type));
    os << ",\"created_at\":" << jsonString(view.created_at);
    os << ",\"updated_at\":" << jsonString(view.updated_at);
    os << ',';
    append_optional_timestamp(os, "expires_at", view.expires_at);
    os << ',';
    append_optional_timestamp(os, "deleted_at", view.deleted_at);
    os << '}';
    return os.str();
}

}  // namespace

LinksTableSource::LinksTableSource(const ILinkLister& lister)
    : m_lister_(lister)
{
    // Literal, fixed column list (contract C6): the columns that exist are
    // exactly the columns db.rows may expose.
    m_columns_ = {
        make_column("id", "string", false, false),
        make_column("slug", "string", true, true),
        make_column("url", "string", false, true),
        make_column("status", "string", false, false),
        make_column("enabled", "bool", false, false),
        make_column("redirect_type", "string", false, false),
        make_column("created_at", "timestamp", true, false),
        make_column("updated_at", "timestamp", true, false),
        make_column("expires_at", "timestamp", false, false),
        make_column("deleted_at", "timestamp", false, false),
    };
}

const std::string& LinksTableSource::name() const
{
    static const std::string k_name = "links";
    return k_name;
}

bool LinksTableSource::writable() const
{
    return true;
}

const std::vector<TableColumn>& LinksTableSource::columns() const
{
    return m_columns_;
}

Result<std::size_t> LinksTableSource::row_count() const
{
    LinkListQuery query;
    // Exclude soft-deleted links from the count for every caller: `db.rows` is
    // a read-permission command, so counting deleted rows here would let a
    // read-only `User` learn how many soft-deleted links exist, bypassing the
    // T-71 gate that `link.list`/`link.export` enforce (status=all|deleted
    // require write). Admins still reach deleted links via
    // `link.list status=deleted`.
    query.status = list_status::not_deleted;
    query.limit = 1;  // We only need the total, not a materialized window.

    const Result<LinkListPage> page = m_lister_.list(query);
    Result<std::size_t> result;
    if (!page.ok()) {
        result.error.code = AppErrorCode::storage_failure;
        result.error.detail = "link listing failed";
        return result;
    }
    result.value = page.value->total;
    return result;
}

Result<TableRowsPage> LinksTableSource::rows(const TableQuery& query) const
{
    LinkListQuery list_query;
    // Exclude soft-deleted links for every caller (active/disabled/expired are
    // all visible). `db.rows` is a read-permission command with no
    // `permission_for` hook, so surfacing deleted rows here would let a
    // read-only `User` bulk-enumerate soft-deleted links, bypassing the T-71
    // gate that `link.list`/`link.export` enforce (status=all|deleted require
    // write). Admins still reach deleted links via `link.list status=deleted`.
    list_query.status = list_status::not_deleted;
    list_query.q = query.q;
    list_query.offset = query.offset;
    list_query.limit = query.limit;
    if (query.sort.has_value()) {
        list_query.sort = to_list_sort(*query.sort);
        list_query.descending = query.descending;
    }

    const Result<LinkListPage> page = m_lister_.list(list_query);
    Result<TableRowsPage> result;
    if (!page.ok()) {
        result.error.code = AppErrorCode::storage_failure;
        result.error.detail = "link listing failed";
        return result;
    }

    TableRowsPage rows_page;
    rows_page.total = page.value->total;
    rows_page.rows_json.reserve(page.value->items.size());
    for (const LinkView& view : page.value->items) {
        rows_page.rows_json.push_back(serialize_link_row(view));
    }
    result.value = std::move(rows_page);
    return result;
}

}  // namespace app
}  // namespace url_shortener
