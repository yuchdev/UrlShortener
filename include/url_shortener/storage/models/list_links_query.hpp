#pragma once

#include <cstddef>
#include <optional>
#include <string>

/**
 * @brief Filter and pagination options for `IMetadataRepository::
 * ListLinks`. Records are sorted by `created_at` ascending before
 * pagination is applied.
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
};
