#pragma once

#include <memory>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/storage/i_metadata_repository.hpp>
#include <url_shortener/storage/models/list_links_query.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief Translates a browse query into the storage-layer `::ListLinksQuery`
 * consumed by `::IMetadataRepository::ListLinksPage`.
 *
 * The translation is the single filter definition shared with the legacy
 * in-memory lister (subtask 13), so both listers are provably equivalent:
 * - `status`, `tag` and `campaign` become the query's `extra_filter`
 *   predicate, which decodes each candidate with `record_to_link` and then
 *   matches on `resolveLinkStatus` (status, unless `all`), an exact tag
 *   (`std::find` over `link.tags`), and an exact `campaign->name`. A record
 *   that fails to decode is excluded (the predicate returns `false`), so a
 *   malformed record never crashes the scan or leaks its raw attributes.
 * - `q` becomes `text` (case-insensitive slug/URL substring).
 * - `sort`/`descending` become `sort_by`/`order`.
 * - `include_inactive` and `include_deleted` are both forced `true` so the
 *   repository returns every lifecycle state and the `extra_filter` above is
 *   the sole status authority (consistent across backends).
 * - `offset`/`limit` are copied verbatim.
 *
 * @param query Browse query to translate; the status/tag/campaign values it
 * holds are copied into the returned predicate, which therefore does not
 * depend on @p query outliving the returned object.
 * @return A `::ListLinksQuery` whose `extra_filter` is a self-contained,
 * non-throwing predicate capturing copies of the browse filters.
 */
::ListLinksQuery build_list_links_query(const LinkListQuery& query);

/**
 * @brief `ILinkLister` backed by the configured `::IMetadataRepository`.
 *
 * Decodes every candidate record through the 02.0 codec and projects it with
 * `LinkCommandService::ViewOf`, so list items are byte-identical to
 * `link.get` results and agree with it about lifecycle status. Uses only
 * `::IMetadataRepository::ListLinksPage`; it issues no SQL and includes no
 * storage driver (constraint: ports only).
 *
 * Lifetime: shares ownership of the repository via `std::shared_ptr`; holds
 * the `LinkCommandService` by const reference and does NOT own it, so the
 * referenced service must outlive this object.
 */
class RepositoryLinkLister final : public ILinkLister
{
public:
    /**
     * @brief Wires the lister to its repository and view projector.
     * @param repository Authoritative metadata repository; shared ownership
     * is taken. Must not be null (a null repository is a composition-time
     * programming error).
     * @param views Projects decoded links into the shared `LinkView`; held by
     * const reference and not owned, so it must outlive this lister.
     */
    RepositoryLinkLister(std::shared_ptr<::IMetadataRepository> repository,
                         const LinkCommandService& views);

    /**
     * @brief Lists link views for @p query.
     *
     * Validates the query (bounded work), translates it with
     * `build_list_links_query`, runs one `ListLinksPage` scan, then decodes
     * and projects each returned record.
     *
     * @param query Browse query.
     * @return `Result<LinkListPage>` on success; `invalid_field` when the
     * query is out of bounds (see `validate_link_list_query`);
     * `storage_failure` with the fixed detail `"list failed"` (never the
     * driver message) when `ListLinksPage` fails; `internal` when a record
     * that passed the scan's predicate cannot be re-decoded (not expected,
     * since the same decode already succeeded inside `extra_filter`).
     */
    Result<LinkListPage> list(const LinkListQuery& query) const override;

private:
    ///< Authoritative metadata repository; shared ownership, never null.
    std::shared_ptr<::IMetadataRepository> m_repository;

    ///< View projector; not owned, must outlive this object.
    const LinkCommandService& m_views;
};

}  // namespace app
}  // namespace url_shortener
