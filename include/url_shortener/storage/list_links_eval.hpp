#pragma once

#include <cstddef>
#include <vector>

#include "url_shortener/storage/models/link_record.hpp"
#include "url_shortener/storage/models/list_links_query.hpp"

/**
 * @brief A single filtered, sorted and paginated window of link records
 * together with the total number of records that matched the query
 * before the `offset`/`limit` window was applied.
 */
struct LinkPage
{
    ///< The requested window: the records in `[offset, offset + limit)`
    ///< of the sorted, filtered result. Empty when `offset >= total`,
    ///< `limit == 0`, or nothing matched.
    std::vector<LinkRecord> items;

    ///< Number of records that matched every filter, counted before the
    ///< `offset`/`limit` window was applied. Independent of `offset` and
    ///< `limit`.
    std::size_t total = 0;
};

/**
 * @brief Upper bound on the number of candidate records the default
 * `IMetadataRepository::ListLinksPage` will scan in a single call.
 *
 * A `ListLinks` result whose size reaches this cap is treated as an
 * overflow (`RepoError::permanent_failure`) rather than silently
 * truncated, so a window is never mistaken for the complete result.
 */
constexpr std::size_t kListScanLimit = 100000;

/**
 * @brief Tests whether a single record satisfies the native filters of a
 * query (the filters a backend's `ListLinks` applies plus the optional
 * free-text and application predicate).
 *
 * Applies, in order: the legacy `include_deleted` flag
 * (`include_deleted == false` drops records with `deleted_at`), the
 * legacy `include_inactive` flag (`include_inactive == false` drops
 * records with `is_active == false`), the `owner` filter (kept only when
 * `record.owner` equals `query.owner`), the `text` filter
 * (case-insensitive ASCII substring of `short_code` or `target_url`),
 * and finally `extra_filter` when set.
 *
 * `extra_filter` is invoked only for records that passed every preceding
 * filter and must not throw (this function does not catch).
 *
 * @param record Candidate record to test.
 * @param query Query whose filters are evaluated against `record`.
 * @return `true` if the record passes every active filter; `false`
 * otherwise.
 */
bool LinkRecordMatches(const LinkRecord& record, const ListLinksQuery& query);

/**
 * @brief Evaluates the extended query over a set of candidate records:
 * filter (via `LinkRecordMatches`) -> stable sort by `query.sort_by` in
 * `query.order` with a `short_code`-ascending tie-break -> count the
 * matches (`LinkPage::total`) -> return the `offset`/`limit` window
 * (`LinkPage::items`).
 *
 * `total` reflects every matching record and is independent of `offset`
 * and `limit`. An `offset` at or beyond `total`, or a `limit` of zero,
 * yields empty `items` with the correct `total`.
 *
 * @param candidates Records to evaluate; taken by value and consumed.
 * @param query Filter, sort and pagination options; `query.extra_filter`
 * must not throw.
 * @return The counted total and the requested window.
 */
LinkPage ApplyListQuery(std::vector<LinkRecord> candidates,
                        const ListLinksQuery& query);
