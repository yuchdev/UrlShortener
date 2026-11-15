#pragma once

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief `ILinkLister` backed by the legacy, `Link`-based in-memory
 * `url_shortener::IMetadataRepository` used in default (no `--storage-config`)
 * mode.
 *
 * The legacy repository has no native list operation, so this lister takes a
 * `snapshot()` copy of every stored `Link` and then runs the exact query
 * semantics of the repository lister (subtask 12): it re-encodes each link to
 * a `::LinkRecord` with the 02.0 codec and evaluates
 * `build_list_links_query(query)` through `ApplyListQuery`. There is therefore
 * one implementation of what a filter means, shared with
 * `RepositoryLinkLister`, and list results are projected through
 * `LinkCommandService::ViewOf` so they are byte-identical to `link.get`.
 *
 * @note The repository parameter is the legacy `url_shortener::IMetadata
 * Repository` (`Link`-based: `create`/`getBySlug`/`getById`/`slugExists`/
 * `update`/`snapshot`), a distinct type from the global record-based
 * `::IMetadataRepository` that `RepositoryLinkLister` uses.
 *
 * Lifetime: holds both the repository and the view projector by const
 * reference and owns neither, so both referenced objects must outlive this
 * lister.
 */
class LegacyLinkLister final : public ILinkLister
{
public:
    /**
     * @brief Wires the lister to its legacy repository and view projector.
     * @param repository Legacy `Link`-based metadata repository to snapshot;
     * held by const reference and not owned, so it must outlive this lister.
     * @param views Projects decoded links into the shared `LinkView`; held by
     * const reference and not owned, so it must outlive this lister.
     */
    LegacyLinkLister(const url_shortener::IMetadataRepository& repository,
                     const LinkCommandService& views);

    /**
     * @brief Lists link views for @p query.
     *
     * Validates the query (bounded work), snapshots the repository, re-encodes
     * each link with the 02.0 codec (a link that fails to encode is skipped
     * and counted toward nothing; this cannot happen for links produced by
     * `LinkCommandService`), evaluates `build_list_links_query` via
     * `ApplyListQuery`, then decodes and projects each record in the window.
     *
     * @param query Browse query.
     * @return `Result<LinkListPage>` on success; `invalid_field` when the
     * query is out of bounds (see `validate_link_list_query`); `internal` with
     * the fixed detail `"decode failed"` when a record that passed filtering
     * cannot be re-decoded (not expected, since it was just encoded from a
     * valid link). This lister performs no I/O and so never returns
     * `storage_failure`.
     */
    Result<LinkListPage> list(const LinkListQuery& query) const override;

private:
    ///< Legacy repository to snapshot; not owned, must outlive this object.
    const url_shortener::IMetadataRepository& m_repository;

    ///< View projector; not owned, must outlive this object.
    const LinkCommandService& m_views;
};

}  // namespace app
}  // namespace url_shortener
