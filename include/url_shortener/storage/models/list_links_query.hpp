#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>

/**
 * @brief Forward declaration of the persisted short-link record so
 * `ListLinksQuery::extra_filter` can take it by const reference without
 * pulling in the full `link_record.hpp` definition here.
 */
struct LinkRecord;

/**
 * @brief Field a listing is ordered by before the pagination window is
 * applied. `created_at` preserves the historical `ListLinks` ordering.
 */
enum class LinkSortField
{
    ///< Order by the record's creation timestamp.
    created_at,

    ///< Order by the record's most-recent-mutation timestamp.
    updated_at,

    ///< Order by the public short-code slug (lexicographic, byte order).
    short_code,
};

/**
 * @brief Direction a listing is sorted in. `asc` preserves the historical
 * `ListLinks` ascending order.
 */
enum class SortOrder
{
    ///< Ascending order (smallest/earliest first).
    asc,

    ///< Descending order (largest/latest first).
    desc,
};

/**
 * @brief Filter, sort and pagination options for listing short-link
 * records. Consumed by `IMetadataRepository::ListLinks` (native owner /
 * inactive / deleted / window handling) and, with the extended fields
 * below, by the pure `ApplyListQuery` helper and
 * `IMetadataRepository::ListLinksPage`.
 *
 * The extended fields default to values that reproduce today's
 * `ListLinks` behavior exactly: no text filter, order by `created_at`
 * ascending, and no application predicate.
 */
struct ListLinksQuery
{
    ///< If set, only records whose `owner` matches this value are
    ///< returned; absent means all owners are included.
    std::optional<std::string> owner;

    ///< If `false`, records with `is_active == false` are excluded.
    bool include_inactive = true;

    ///< If `false`, records with `deleted_at.has_value()` are
    ///< excluded (i.e. soft-deleted links are hidden by default).
    bool include_deleted = false;

    ///< Zero-based index of the first record to return after
    ///< filtering and sorting; zero means start from the beginning.
    std::size_t offset = 0;

    ///< Maximum number of records to return; zero returns nothing.
    std::size_t limit = 100;

    ///< If set, keeps only records whose `short_code` or `target_url`
    ///< contains this value as a case-insensitive (ASCII) substring;
    ///< absent means no text filter is applied.
    std::optional<std::string> text;

    ///< Field the matching records are ordered by before the
    ///< `offset`/`limit` window is applied.
    LinkSortField sort_by = LinkSortField::created_at;

    ///< Direction records are ordered in; `asc` keeps today's behavior.
    SortOrder order = SortOrder::asc;

    ///< Optional application-layer predicate evaluated as a final
    ///< filter after the native filters above; a record is kept only
    ///< when the predicate returns `true`. An empty target keeps every
    ///< record. The predicate must not throw: it is invoked only with
    ///< records that already passed the native filters and callers of
    ///< `ApplyListQuery`/`ListLinksPage` do not catch exceptions from
    ///< it.
    std::function<bool(const LinkRecord&)> extra_filter;
};
