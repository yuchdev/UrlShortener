#pragma once

#include <url_shortener/app/link_command_service.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief `ILinkStore` implementation that delegates to the
 * process-global `linkRepository()` and `linkCache()` singletons
 * (see `include/url_shortener/storage/link_repository.hpp`).
 *
 * This is the legacy bridge that connects `LinkCommandService` to
 * the global in-memory storage initialized at process startup. It
 * owns no state of its own; all data lives in the singletons, which
 * are process-lifetime objects.
 *
 * Thread-safety: all methods are as thread-safe as the underlying
 * `InMemoryMetadataRepository` and `InMemoryLinkCacheStore`
 * singletons, both of which guard their state with
 * `std::shared_mutex`.
 */
class LegacyLinkStore final : public ILinkStore
{
public:
    /**
     * @brief Writes `link` to the global repository.
     * @param link Link record to persist; `link.id` and `link.slug`
     * must already be set by the caller.
     * @param[out] error If non-null and the call fails, receives
     * `AppErrorCode::slug_conflict` when the slug or ID is already
     * in use, or `AppErrorCode::storage_failure` for any other
     * repository error; left untouched on success.
     * @return `true` if the link was stored; `false` if the slug is
     * already in use or the repository reported an error.
     */
    bool create(const Link& link, AppError* error = nullptr) override;

    /**
     * @brief Looks up a link by its slug, consulting the in-process
     * link cache first. On a cache miss, falls back to the global
     * repository and populates the cache on a hit.
     * @param slug Short-code slug to look up.
     * @return The matching link; `std::nullopt` if not found.
     */
    std::optional<Link> findBySlug(const std::string& slug) const override;

    /**
     * @brief Looks up a link by its stable identifier. Bypasses the
     * cache and reads directly from the global repository.
     * @param id Stable link identifier to look up.
     * @return The matching link; `std::nullopt` if not found.
     */
    std::optional<Link> findById(const std::string& id) const override;

    /**
     * @brief Checks whether the given slug is registered in the
     * global repository.
     * @param slug Short-code slug to check.
     * @return `true` if the slug exists; `false` otherwise.
     */
    bool slugExists(const std::string& slug) const override;

    /**
     * @brief Evicts the entry for `slug` from the in-process link
     * cache. A no-op if the slug is not currently cached.
     * @param slug Short-code slug whose cache entry to evict.
     */
    void invalidateCache(const std::string& slug) override;

    /**
     * @brief Persists a full-record update for an existing link and
     * evicts its cache entry atomically via
     * `updateLinkAndInvalidateCache`.
     * @param link Updated link record; must already exist in the
     * repository (looked up by `link.id`).
     * @param[out] error If non-null and the call fails, receives
     * `AppErrorCode::storage_failure`; left untouched on success.
     * @return `true` if the update was persisted and the cache entry
     * evicted; `false` if the repository call failed (e.g. the
     * record no longer exists).
     */
    bool update(const Link& link, AppError* error = nullptr) override;
};

/**
 * @brief `ILinkStatsReader` implementation that queries the
 * process-global `analyticsClickRepository()` in-memory store.
 *
 * Parses `GetLinkStatsQuery::from` and `to` as Unix epoch-second
 * integer strings, translates `bucket` to an `AggregateBucket`
 * value, and delegates to
 * `InMemoryClickEventRepository::GetAggregateStats`.
 *
 * Thread-safety: `read` is as thread-safe as the underlying
 * `InMemoryClickEventRepository` singleton it queries.
 */
class LegacyLinkStatsReader final : public ILinkStatsReader
{
public:
    /**
     * @brief Reads aggregate click-event statistics for the slug
     * and time range specified in `query`.
     * @param query Fields: `slug` (required, non-empty); `from` and
     * `to` as Unix epoch-second integer strings with `from < to`;
     * `bucket` one of `"hour"`, `"day"`, or `"week"`.
     * @return A populated `Result<LinkStatsView>` on success;
     * `AppErrorCode::not_found` if `query.slug` is empty;
     * `AppErrorCode::invalid_field` if `from`/`to` cannot be parsed
     * as integers, `from >= to`, or `bucket` is unrecognized;
     * `AppErrorCode::storage_failure` if `GetAggregateStats` fails.
     */
    Result<LinkStatsView> read(const GetLinkStatsQuery& query) const override;
};

}  // namespace app
}  // namespace url_shortener
