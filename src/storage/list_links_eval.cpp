#include <algorithm>
#include <cctype>
#include <string>

#include "url_shortener/storage/list_links_eval.hpp"

#include "url_shortener/storage/i_metadata_repository.hpp"

namespace
{

/**
 * @brief Lower-cases a single byte using the ASCII mapping only, leaving
 * non-ASCII bytes untouched (locale-independent, so text matching is
 * stable across platforms).
 * @param c Byte to fold.
 * @return The lower-case ASCII equivalent of `c`, or `c` unchanged.
 */
char AsciiToLower(char c)
{
    const unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') {
        return static_cast<char>(uc - 'A' + 'a');
    }
    return c;
}

/**
 * @brief Tests whether `needle` is a case-insensitive (ASCII) substring
 * of `haystack`.
 * @param haystack String searched within.
 * @param needle Substring to search for; an empty needle always matches.
 * @return `true` if `haystack` contains `needle` ignoring ASCII case.
 */
bool ContainsCaseInsensitive(const std::string& haystack,
                             const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    const std::size_t last = haystack.size() - needle.size();
    for (std::size_t start = 0; start <= last; ++start) {
        std::size_t offset = 0;
        for (; offset < needle.size(); ++offset) {
            if (AsciiToLower(haystack[start + offset])
                != AsciiToLower(needle[offset]))
            {
                break;
            }
        }
        if (offset == needle.size()) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Orders two records by the query's sort field and direction,
 * with a `short_code`-ascending tie-break so the sort is deterministic.
 * @param lhs Left record.
 * @param rhs Right record.
 * @param query Query carrying `sort_by` and `order`.
 * @return `true` if `lhs` should sort before `rhs`.
 */
bool RecordLess(const LinkRecord& lhs,
                const LinkRecord& rhs,
                const ListLinksQuery& query)
{
    bool less_by_field = false;
    bool equal_by_field = false;
    switch (query.sort_by) {
        case LinkSortField::created_at:
            less_by_field = lhs.created_at < rhs.created_at;
            equal_by_field = lhs.created_at == rhs.created_at;
            break;
        case LinkSortField::updated_at:
            less_by_field = lhs.updated_at < rhs.updated_at;
            equal_by_field = lhs.updated_at == rhs.updated_at;
            break;
        case LinkSortField::short_code:
            less_by_field = lhs.short_code < rhs.short_code;
            equal_by_field = lhs.short_code == rhs.short_code;
            break;
    }
    if (equal_by_field) {
        // Tie-break is always short_code ascending, regardless of order.
        return lhs.short_code < rhs.short_code;
    }
    return query.order == SortOrder::asc ? less_by_field : !less_by_field;
}

}  // namespace

bool LinkRecordMatches(const LinkRecord& record, const ListLinksQuery& query)
{
    if (!query.include_deleted && record.deleted_at.has_value()) {
        return false;
    }
    if (!query.include_inactive && !record.is_active) {
        return false;
    }
    if (query.owner.has_value() && record.owner != query.owner) {
        return false;
    }
    if (query.text.has_value()) {
        const std::string& needle = *query.text;
        if (!ContainsCaseInsensitive(record.short_code, needle)
            && !ContainsCaseInsensitive(record.target_url, needle))
        {
            return false;
        }
    }
    if (query.extra_filter && !query.extra_filter(record)) {
        return false;
    }
    return true;
}

LinkPage ApplyListQuery(std::vector<LinkRecord> candidates,
                        const ListLinksQuery& query)
{
    std::vector<LinkRecord> matched;
    matched.reserve(candidates.size());
    for (auto& record : candidates) {
        if (LinkRecordMatches(record, query)) {
            matched.push_back(std::move(record));
        }
    }

    std::stable_sort(matched.begin(),
                     matched.end(),
                     [&query](const LinkRecord& lhs, const LinkRecord& rhs)
                     { return RecordLess(lhs, rhs, query); });

    LinkPage page;
    page.total = matched.size();

    if (query.offset >= matched.size() || query.limit == 0) {
        return page;
    }

    const std::size_t remaining = matched.size() - query.offset;
    const std::size_t count = std::min(query.limit, remaining);
    const std::size_t end = query.offset + count;
    using DifferenceType = std::vector<LinkRecord>::difference_type;
    page.items.assign(
        std::make_move_iterator(matched.begin()
                                + static_cast<DifferenceType>(query.offset)),
        std::make_move_iterator(matched.begin()
                                + static_cast<DifferenceType>(end)));
    return page;
}

bool IMetadataRepository::ListLinksPage(const ListLinksQuery& query,
                                        LinkPage* page,
                                        RepoError* error) const
{
    if (page == nullptr) {
        if (error != nullptr) {
            *error = RepoError::invalid_argument;
        }
        return false;
    }

    ListLinksQuery base = query;
    base.offset = 0;
    base.limit = kListScanLimit;

    RepoError list_error = RepoError::none;
    std::vector<LinkRecord> candidates = ListLinks(base, &list_error);
    if (list_error != RepoError::none) {
        if (error != nullptr) {
            *error = list_error;
        }
        return false;
    }
    if (candidates.size() >= kListScanLimit) {
        if (error != nullptr) {
            *error = RepoError::permanent_failure;
        }
        return false;
    }

    *page = ApplyListQuery(std::move(candidates), query);
    if (error != nullptr) {
        *error = RepoError::none;
    }
    return true;
}
