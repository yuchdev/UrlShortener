#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/app/table_source.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief The `links` logical table for `db.*`, projecting @ref ILinkLister
 * results over the fixed column list of milestone decision Q7 (contract C6).
 *
 * The column list is a literal constant declared by this source, never derived
 * from `LinkView` reflection or a repository result, so the columns that exist
 * are exactly the columns `db.rows` may expose. Rows are flat scalar objects
 * whose keys equal the column names; no `tags`, `metadata`, `campaign`,
 * `short_url` or any other `LinkView` field is surfaced. Browse queries always
 * use `list_status::all` (the TUI shows every lifecycle state) and map the
 * admissible `sort`/`q` of a @ref TableQuery onto a @ref LinkListQuery.
 *
 * Thread-safety follows the backing @ref ILinkLister.
 */
class LinksTableSource final : public ITableSource
{
public:
    /**
     * @brief Builds the source over a lister.
     *
     * Ownership: @p lister is borrowed, not owned. The reference is captured
     * for this source's whole lifetime, so @p lister must outlive this object
     * (it is normally a process-lifetime service held by the composition root).
     *
     * @param lister The link browse port every query and count is delegated to.
     */
    explicit LinksTableSource(const ILinkLister& lister);

    /**
     * @brief The logical table name.
     * @return The constant `"links"`.
     */
    const std::string& name() const override;

    /**
     * @brief Whether a typed write command exists for this table.
     * @return Always `true` (the `link.*` commands mutate links); advisory
     * only, there is no write path through `db.*`.
     */
    bool writable() const override;

    /**
     * @brief The fixed `links` column list, in display order.
     * @return `id, slug, url, status, enabled, redirect_type, created_at,
     * updated_at, expires_at, deleted_at`; `slug` and `created_at`/`updated_at`
     * are the only sortable columns, `slug`/`url` the only searchable ones.
     */
    const std::vector<TableColumn>& columns() const override;

    /**
     * @brief The total number of links across every lifecycle status.
     * @return `Result<std::size_t>` holding the lister's `total` for a
     * `status=all` query (requested with `limit == 1` so no window is
     * materialized); on a lister failure a `storage_failure`.
     */
    Result<std::size_t> row_count() const override;

    /**
     * @brief Returns a filtered, counted page of link rows.
     * @param query A bounded, pre-validated browse query; its `sort` (when set)
     * is guaranteed to name one of this source's sortable columns.
     * @return `Result<TableRowsPage>` whose `rows_json` holds one flat,
     * `jsonString`-escaped object per link (keys equal the column names,
     * absent optional timestamps emitted as `null`) and whose `total` is the
     * `status=all` filtered count before the window; on a lister failure a
     * `storage_failure`.
     */
    Result<TableRowsPage> rows(const TableQuery& query) const override;

private:
    ///< Borrowed link browse port; not owned, must outlive this source (see
    ///< the constructor's ownership note).
    const ILinkLister& m_lister_;

    ///< The fixed `links` column list, built once in the constructor and
    ///< returned by @ref columns; never derived from a result (contract C6).
    std::vector<TableColumn> m_columns_;
};

}  // namespace app
}  // namespace url_shortener
