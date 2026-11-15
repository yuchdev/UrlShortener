/**
 * @file repository_link_lister.cpp
 * @brief `RepositoryLinkLister` and `build_list_links_query`: the
 * `::IMetadataRepository`-backed implementation of the application-level
 * `link.list` browse port (subtask 12). Filters mean exactly what
 * `link.get` means, because every candidate is decoded with the 02.0 codec
 * and classified with `resolveLinkStatus`.
 */
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_lister.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>
#include <url_shortener/storage/list_links_eval.hpp>
#include <url_shortener/storage/models/link_record.hpp>

namespace url_shortener
{
namespace app
{
namespace
{

/**
 * @brief Maps a browse `list_status` (non-`all`) to the `LinkStatus` a
 * decoded link must resolve to in order to match.
 * @param status Browse status filter; must not be `list_status::all` (the
 * caller handles `all` without calling this).
 * @return The corresponding `LinkStatus`.
 */
LinkStatus to_link_status(list_status status)
{
    switch (status) {
        case list_status::active:
            return LinkStatus::active;
        case list_status::disabled:
            return LinkStatus::disabled;
        case list_status::expired:
            return LinkStatus::expired;
        case list_status::deleted:
            return LinkStatus::deleted;
        case list_status::all:
        default:
            // `all` is filtered out before this is reached; return `active` as
            // a safe, defined default so the switch is total.
            return LinkStatus::active;
    }
}

/**
 * @brief Maps a browse `list_sort` to the storage-layer `LinkSortField`.
 * @param sort Browse sort field.
 * @return The corresponding `LinkSortField`.
 */
LinkSortField to_sort_field(list_sort sort)
{
    switch (sort) {
        case list_sort::slug:
            return LinkSortField::short_code;
        case list_sort::updated_at:
            return LinkSortField::updated_at;
        case list_sort::created_at:
        default:
            return LinkSortField::created_at;
    }
}

}  // namespace

::ListLinksQuery build_list_links_query(const LinkListQuery& query)
{
    ::ListLinksQuery out;

    // Status filtering is done entirely in the predicate over decoded links,
    // so include every lifecycle state at the native layer and let
    // `extra_filter` be the sole authority (consistent across backends).
    out.include_inactive = true;
    out.include_deleted = true;
    out.offset = query.offset;
    out.limit = query.limit;
    out.text = query.q;
    out.sort_by = to_sort_field(query.sort);
    out.order = query.descending ? SortOrder::desc : SortOrder::asc;

    // Capture copies so the predicate is self-contained and does not depend on
    // `query` outliving the returned `::ListLinksQuery`.
    out.extra_filter = [status = query.status,
                        tag = query.tag,
                        campaign = query.campaign](const ::LinkRecord& record)
    {
        Link link;
        std::string error;
        if (!record_to_link(record, &link, &error)) {
            // A record that cannot be decoded (malformed `lnk_*` attribute,
            // unsupported schema) is excluded rather than surfaced: no crash,
            // no raw-attribute leak (subtask 12 requirement 3).
            return false;
        }
        if (status != list_status::all
            && resolveLinkStatus(link) != to_link_status(status))
        {
            return false;
        }
        if (tag.has_value()
            && std::find(link.tags.begin(), link.tags.end(), *tag)
                == link.tags.end())
        {
            return false;
        }
        if (campaign.has_value()
            && !(link.campaign.has_value() && link.campaign->name.has_value()
                 && *link.campaign->name == *campaign))
        {
            return false;
        }
        return true;
    };

    return out;
}

RepositoryLinkLister::RepositoryLinkLister(
    std::shared_ptr<::IMetadataRepository> repository,
    const LinkCommandService& views)
    : m_repository(std::move(repository))
    , m_views(views)
{
    if (m_repository == nullptr) {
        throw std::invalid_argument(
            "RepositoryLinkLister requires a non-null repository");
    }
}

Result<LinkListPage> RepositoryLinkLister::list(
    const LinkListQuery& query) const
{
    const AppError validation = validate_link_list_query(query);
    if (validation.code != AppErrorCode::none) {
        return {std::nullopt, validation};
    }

    const ::ListLinksQuery repo_query = build_list_links_query(query);

    ::LinkPage page;
    ::RepoError repo_error = ::RepoError::none;
    if (!m_repository->ListLinksPage(repo_query, &page, &repo_error)) {
        // Collapse every backend error to a fixed detail; the driver message
        // is never surfaced (threat model T-54, fixed-string rule).
        return {std::nullopt,
                AppError {AppErrorCode::storage_failure, "list failed"}};
    }

    LinkListPage result;
    result.total = page.total;
    result.offset = query.offset;
    result.limit = query.limit;
    result.items.reserve(page.items.size());
    for (const ::LinkRecord& record : page.items) {
        Link link;
        std::string error;
        if (!record_to_link(record, &link, &error)) {
            // Cannot occur for an item that passed `extra_filter` (the same
            // decode already succeeded there); treat a late failure as an
            // internal invariant breach rather than a client error.
            return {std::nullopt,
                    AppError {AppErrorCode::internal, "decode failed"}};
        }
        result.items.push_back(m_views.ViewOf(link));
    }

    return {std::move(result), AppError {}};
}

}  // namespace app
}  // namespace url_shortener
