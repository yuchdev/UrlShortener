#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_command_service.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief Lifecycle filter applied to a `link.list` browse query.
 *
 * The non-`all` members correspond one-for-one to `LinkStatus` as computed by
 * `resolveLinkStatus` (precedence `deleted > disabled > expired > active`), so
 * a list filtered by a given status returns exactly the links `link.get` would
 * report with that status. `active` therefore excludes expired links.
 *
 * @note This enum deliberately adds `expired` to the `all|active|disabled|
 * deleted` set the command catalog (3.2) lists; the descriptor is
 * authoritative and the catalog is reconciled in Task 08.0.
 */
enum class list_status
{
    ///< No lifecycle filter: every link is returned regardless of status.
    all,

    ///< Only links whose `resolveLinkStatus` is `active` (enabled, not
    ///< expired, not soft-deleted).
    active,

    ///< Only links whose `resolveLinkStatus` is `disabled`
    ///< (`enabled == false`, not soft-deleted).
    disabled,

    ///< Only links whose `resolveLinkStatus` is `expired`
    ///< (`expires_at` in the past, enabled, not soft-deleted).
    expired,

    ///< Only links whose `resolveLinkStatus` is `deleted`
    ///< (`deleted_at` is set).
    deleted
};

/**
 * @brief Field a `link.list` result is ordered by before the
 * `offset`/`limit` window is applied.
 */
enum class list_sort
{
    ///< Order by the link's creation timestamp.
    created_at,

    ///< Order by the public short-code slug (lexicographic byte order).
    slug,

    ///< Order by the link's most-recent-mutation timestamp.
    updated_at
};

/**
 * @brief Browse query for the application-level `link.list` port.
 *
 * The defaults reproduce the command catalog's defaults: only `active` links
 * (decision-log Q9), newest first, first 50 rows. All filters are evaluated
 * against the decoded `Link` (via the 02.0 codec and `resolveLinkStatus`), so
 * the lister never disagrees with `link.get`.
 */
struct LinkListQuery
{
    ///< Lifecycle filter; defaults to `active` (decision-log Q9).
    list_status status = list_status::active;

    ///< Optional free-text filter: kept only when this value is a
    ///< case-insensitive (ASCII) substring of the slug or the target URL;
    ///< absent means no text filter. Bounded to
    ///< `link_list_max_query_length` by `validate_link_list_query`.
    std::optional<std::string> q;

    ///< Optional exact-match tag filter: kept only when the link carries
    ///< this tag verbatim; absent means no tag filter.
    std::optional<std::string> tag;

    ///< Optional exact-match campaign filter: kept only when the link's
    ///< `campaign->name` equals this value; absent means no campaign filter.
    std::optional<std::string> campaign;

    ///< Field the matching links are ordered by before the window is
    ///< applied; defaults to `created_at`.
    list_sort sort = list_sort::created_at;

    ///< `true` sorts descending (newest/largest first); `false` ascending.
    bool descending = true;

    ///< Zero-based index of the first link to return after filtering and
    ///< sorting; no upper bound (a value at or beyond `total` yields an
    ///< empty window with the correct `total`).
    std::size_t offset = 0;

    ///< Maximum number of links to return; must be in `1..500` (validated
    ///< by `validate_link_list_query`).
    std::size_t limit = 50;
};

/**
 * @brief One filtered, sorted and counted window of `link.list` results.
 *
 * `total` counts every link matching the query's filters before the
 * `offset`/`limit` window was applied and is therefore stable across pages.
 */
struct LinkListPage
{
    ///< The requested window: the projected views in
    ///< `[offset, offset + limit)` of the sorted, filtered result. Empty
    ///< when `offset >= total`, `limit == 0`, or nothing matched.
    std::vector<LinkView> items;

    ///< Number of links that matched every filter, counted before the
    ///< `offset`/`limit` window was applied; independent of `offset`/`limit`.
    std::size_t total = 0;

    ///< Echo of the query `offset` this window was produced for.
    std::size_t offset = 0;

    ///< Echo of the query `limit` this window was produced for.
    std::size_t limit = 0;
};

///< Inclusive upper bound (and implicit lower bound of 1) on
///< `LinkListQuery::limit`; a limit of 0 or above this is `invalid_field`.
constexpr std::size_t link_list_max_limit = 500;

///< Inclusive upper bound, in bytes, on `LinkListQuery::q`; a longer value is
///< `invalid_field`.
constexpr std::size_t link_list_max_query_length = 256;

/**
 * @brief Validates the bounded-work constraints of a browse query.
 *
 * Enforces exactly two bounds, in this order: `limit` must be in
 * `1..link_list_max_limit`, and `q` (when present) must be at most
 * `link_list_max_query_length` bytes. `offset` has no upper bound (a
 * `std::size_t` is always `>= 0`). The returned error `detail` names the
 * offending field only (`"limit"` or `"q"`) and never echoes the supplied
 * value, so a hostile argument can never reach a log or response through the
 * error channel.
 *
 * @param query Query to validate; not mutated.
 * @return An `AppError` with code `none` when the query is within bounds;
 * otherwise code `invalid_field` with `detail == "limit"` when `limit` is 0 or
 * exceeds `link_list_max_limit`, or `detail == "q"` when `q` exceeds
 * `link_list_max_query_length` bytes (`limit` is checked first).
 */
AppError validate_link_list_query(const LinkListQuery& query);

/**
 * @brief Application-level browse port: a query to a page of `LinkView`.
 *
 * Implementations own the *meaning* of the filters (lifecycle status, tag,
 * campaign, free text) and must project items through the same `LinkView`
 * path as `link.get` so list results can never disagree with a single-link
 * read. Thread-safety follows the backing service/repository.
 */
class ILinkLister
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed through a
     * base `ILinkLister*`.
     */
    virtual ~ILinkLister() = default;

    /**
     * @brief Returns a filtered, sorted and counted page of link views.
     * @param query Browse query; validated for bounded work before any scan.
     * @return `Result<LinkListPage>` populated on success; on failure
     * `invalid_field` (query out of bounds, see `validate_link_list_query`),
     * `storage_failure` (the backing store failed), or `internal` (a record
     * that passed filtering could not be re-decoded).
     */
    virtual Result<LinkListPage> list(const LinkListQuery& query) const = 0;
};

/**
 * @brief Serializes a `LinkListPage` to its canonical JSON object.
 *
 * Emits, in this order, `"items"` (a JSON array of `serializeLinkViewJson`
 * objects, so each item is byte-identical to a `link.get` body), then the
 * bare integers `"offset"`, `"limit"` and `"total"`:
 * `{"items":[...],"offset":N,"limit":N,"total":N}`.
 *
 * @param page Page to serialize; its `items` are emitted via
 * `serializeLinkViewJson` in order.
 * @return The canonical list JSON object for @p page.
 */
std::string serializeLinkListJson(const LinkListPage& page);

}  // namespace app
}  // namespace url_shortener
