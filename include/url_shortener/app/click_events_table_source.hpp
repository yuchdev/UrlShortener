#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <url_shortener/analytics/i_click_event_repository.hpp>
#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/table_source.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief The `click_events` logical table for `db.*`, projecting
 * @ref analytics::IClickEventRepository::ListEvents results over the fixed
 * column list of milestone decision Q7 (contract C6).
 *
 * Security-critical projection: the column list is a literal constant declared
 * here, never derived from `ClickEvent` reflection or a repository result, so
 * the only client identifier that can ever be exposed is the HMAC
 * `client_id_hash` - no IP, raw client id or fingerprint column exists. The
 * untrusted free-text cells (`referrer`, `user_agent`) are
 * `jsonString`-escaped; `referrer` additionally has its query string and
 * fragment stripped before emission so a captured `?token=`/`#...` value cannot
 * leak (threat model T-52). Repository failures are mapped to a fixed
 * `storage_failure` detail; the backend `AnalyticsError::message` (which may
 * carry driver text, CWE-209) is never forwarded.
 *
 * The table is read-only (`writable() == false`): there is no typed click
 * mutation command. Thread-safety follows the backing repository.
 */
class ClickEventsTableSource final : public ITableSource
{
public:
    /**
     * @brief Builds the source over a click-event repository.
     *
     * Ownership: @p repository is borrowed, not owned. The reference is
     * captured for this source's whole lifetime, so @p repository must outlive
     * this object (the composition root for this milestone passes the
     * process-global analytics click repository).
     *
     * @param repository The click-event store every query and count is
     * delegated to via `ListEvents`.
     */
    explicit ClickEventsTableSource(
        analytics::IClickEventRepository& repository);

    /**
     * @brief The logical table name.
     * @return The constant `"click_events"`.
     */
    const std::string& name() const override;

    /**
     * @brief Whether a typed write command exists for this table.
     * @return Always `false`; click events have no typed mutation command and
     * `db.*` has no write path regardless.
     */
    bool writable() const override;

    /**
     * @brief The fixed `click_events` column list, in display order.
     * @return `event_id, occurred_at, slug, link_id, domain, status_code,
     * referrer, user_agent, client_id_hash`; `occurred_at`, `slug` and
     * `status_code` are sortable, `slug`/`domain` searchable. `client_id_hash`
     * is the only client identifier.
     */
    const std::vector<TableColumn>& columns() const override;

    /**
     * @brief The total number of stored click events.
     * @return `Result<std::size_t>` holding the repository's match total for an
     * unfiltered list (requested with `limit == 0` so no window is
     * materialized); on a repository failure a `storage_failure` with a fixed,
     * value-free detail.
     */
    Result<std::size_t> row_count() const override;

    /**
     * @brief Returns a filtered, counted page of click-event rows.
     * @param query A bounded, pre-validated browse query; its `sort` (when set)
     * is guaranteed to name one of this source's sortable columns. `limit` is
     * additionally clamped to @ref analytics::kMaxClickEventPageSize.
     * @return `Result<TableRowsPage>` whose `rows_json` holds one flat,
     * `jsonString`-escaped object per event (keys equal the column names,
     * `referrer` query/fragment stripped) and whose `total` is the filtered
     * match count before the window; on a repository failure a
     * `storage_failure` with a fixed, value-free detail.
     */
    Result<TableRowsPage> rows(const TableQuery& query) const override;

private:
    ///< Borrowed click-event store; not owned, must outlive this source (see
    ///< the constructor's ownership note).
    analytics::IClickEventRepository& m_repository_;

    ///< The fixed `click_events` column list, built once in the constructor and
    ///< returned by @ref columns; never derived from a result (contract C6).
    std::vector<TableColumn> m_columns_;
};

}  // namespace app
}  // namespace url_shortener
