/**
 * @file legacy_link_lister.cpp
 * @brief `LegacyLinkLister`: the `url_shortener::IMetadataRepository`-backed
 * (legacy, `Link`-based, in-memory) implementation of the application-level
 * `link.list` browse port (subtask 13).
 *
 * Reuses subtask 12's one query implementation: it snapshots the legacy
 * repository, re-encodes each `Link` to a `::LinkRecord` with the 02.0 codec
 * and evaluates `build_list_links_query(query)` through `ApplyListQuery`, so
 * filters mean exactly what they mean for `RepositoryLinkLister` (and for
 * `link.get`).
 */
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/app/legacy_link_lister.hpp>
#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_lister.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/list_links_eval.hpp>
#include <url_shortener/storage/models/link_record.hpp>

namespace url_shortener
{
namespace app
{

LegacyLinkLister::LegacyLinkLister(
    const url_shortener::IMetadataRepository& repository,
    const LinkCommandService& views)
    : m_repository(repository)
    , m_views(views)
{
}

Result<LinkListPage> LegacyLinkLister::list(const LinkListQuery& query) const
{
    const AppError validation = validate_link_list_query(query);
    if (validation.code != AppErrorCode::none) {
        return {std::nullopt, validation};
    }

    // Copy under a shared lock only; no filtering or decoding happens while the
    // repository lock is held (that would stall redirects/writes).
    const std::vector<Link> links = m_repository.snapshot();

    std::vector<::LinkRecord> records;
    records.reserve(links.size());
    for (const Link& link : links) {
        ::LinkRecord record;
        std::string error;
        if (!link_to_record(link, &record, &error)) {
            // A link that cannot be re-encoded (e.g. an unparseable timestamp)
            // is skipped and counts toward nothing. This cannot happen for
            // links produced by `LinkCommandService`, which validates
            // timestamps before persisting.
            continue;
        }
        records.push_back(std::move(record));
    }

    // One query implementation shared with the repository lister (subtask 12):
    // same filter predicate, sort and pagination semantics.
    ::LinkPage page =
        ApplyListQuery(std::move(records), build_list_links_query(query));

    LinkListPage result;
    result.total = page.total;
    result.offset = query.offset;
    result.limit = query.limit;
    result.items.reserve(page.items.size());
    for (const ::LinkRecord& record : page.items) {
        Link link;
        std::string error;
        if (!record_to_link(record, &link, &error)) {
            // Cannot occur: the record was just encoded from a valid link and
            // passed the same decode inside `extra_filter`. Treat a late
            // failure as an internal invariant breach, not a client error, and
            // never surface the (fixed) detail as a value.
            return {std::nullopt,
                    AppError {AppErrorCode::internal, "decode failed"}};
        }
        result.items.push_back(m_views.ViewOf(link));
    }

    return {std::move(result), AppError {}};
}

}  // namespace app
}  // namespace url_shortener
