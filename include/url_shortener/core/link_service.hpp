#pragma once

#include <optional>
#include <ostream>
#include <string>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/i_analytics_sink.hpp"
#include "url_shortener/storage/i_cache_store.hpp"
#include "url_shortener/storage/i_metadata_repository.hpp"
#include "url_shortener/storage/models/rate_limit_decision.hpp"

/**
 * @brief Outcome categories for a slug-resolution attempt.
 */
enum class ResolveStatus
{
    ///< A matching, active, non-expired link was found; the caller
    ///< should issue a redirect to `ResolveResult::target_url`.
    redirect,

    ///< No link with the given short code exists in the repository
    ///< or cache.
    not_found,

    ///< A matching link exists but `is_active` is `false`
    ///< (disabled); the caller should return HTTP 410.
    inactive,

    ///< A matching active link exists but its expiry timestamp is
    ///< in the past; the caller should return HTTP 410.
    expired,
};

/**
 * @brief Stream formatter for `ResolveStatus`.
 * @param os Stream to write to.
 * @param e Value to format.
 * @return `os` with the qualified name (e.g.
 * `"ResolveStatus::redirect"`) written.
 */
inline std::ostream& operator<<(std::ostream& os, ResolveStatus e)
{
    switch (e) {
        case ResolveStatus::redirect:
            return os << "ResolveStatus::redirect";
        case ResolveStatus::not_found:
            return os << "ResolveStatus::not_found";
        case ResolveStatus::inactive:
            return os << "ResolveStatus::inactive";
        case ResolveStatus::expired:
            return os << "ResolveStatus::expired";
    }
    return os << "ResolveStatus(" << static_cast<int>(e) << ")";
}

/**
 * @brief Output of a single slug-resolution attempt.
 *
 * `target_url` is set only when `status == ResolveStatus::redirect`.
 * `cache_hit` reflects whether the result was served from cache; it
 * is always `false` for non-redirect outcomes.
 */
struct ResolveResult
{
    ///< Outcome category of the resolution attempt.
    ResolveStatus status = ResolveStatus::not_found;

    ///< Normalized destination URL; present only when
    ///< `status == ResolveStatus::redirect`.
    std::optional<std::string> target_url;

    ///< `true` if the result was served from the cache without a
    ///< repository lookup; `false` for cache misses and all
    ///< non-redirect outcomes.
    bool cache_hit = false;
};

/**
 * @brief Core redirect-path service: resolves a short code to its
 * target URL through the cache-aside pattern, and coordinates
 * cache invalidation on updates and deletes.
 *
 * Every `Resolve` call emits an analytics event regardless of
 * outcome (see `EmitAnalytics`). Analytics failures are silent and
 * never affect the returned result.
 *
 * Thread-safety: `LinkService` itself holds no mutable state beyond
 * the injected references; thread-safety depends on the
 * implementations of `IMetadataRepository`, `ICacheStore`, and
 * `IAnalyticsSink`.
 *
 * Lifetime: does not own `repo`, `cache`, `analytics`, or `clock`;
 * the caller (composition root) must keep all four alive for at
 * least as long as this object exists.
 */
class LinkService
{
public:
    /**
     * @brief Stores references to the four dependency ports; does
     * not start any background work.
     * @param repo Metadata repository for authoritative link
     * lookups; must outlive this object.
     * @param cache Cache-aside store for fast-path lookups; must
     * outlive this object.
     * @param analytics Analytics event sink called on every
     * resolution attempt; must outlive this object.
     * @param clock Time source used to evaluate link expiry;
     * must outlive this object.
     */
    LinkService(IMetadataRepository& repo,
                ICacheStore& cache,
                IAnalyticsSink& analytics,
                const IClock& clock);

    /**
     * @brief Resolves `short_code` to its redirect target via the
     * cache-aside pattern, then emits an analytics event.
     *
     * Lookup order:
     * 1. `ICacheStore::Get` — on a live, active, non-expired cache
     *    hit, returns `redirect` with `cache_hit = true`.
     * 2. If the cache entry is expired, it is deleted from the cache
     *    before falling through.
     * 3. `IMetadataRepository::GetByShortCode` — on a miss or stale
     *    cache, checks the authoritative store and populates the
     *    cache on a successful redirect lookup.
     *
     * An analytics event is always emitted regardless of outcome;
     * analytics failures are swallowed.
     * @param short_code Short-code slug to resolve.
     * @return `ResolveResult` with the outcome category, target URL
     * (when redirecting), and cache-hit flag.
     */
    ResolveResult Resolve(const std::string& short_code);

    /**
     * @brief Persists a full link-record update and invalidates
     * the cache entry for the record's short code.
     *
     * The cache entry is deleted unconditionally, even if the
     * repository write fails, to avoid serving stale data.
     * @param record Updated link record to persist.
     * @param[out] error If non-null and the repository call fails,
     * receives the error code set by the repository implementation;
     * left untouched on success.
     * @return `true` if the repository accepted the update; `false`
     * if the repository returned `false` (e.g. `not_found` or a
     * backend failure).
     */
    bool Update(const LinkRecord& record, RepoError* error = nullptr);

    /**
     * @brief Deletes the link identified by `short_code` from the
     * repository and invalidates its cache entry.
     *
     * The cache entry is deleted unconditionally, even if the
     * repository write fails, to avoid serving stale data.
     * @param short_code Slug of the link to delete.
     * @param[out] error If non-null and the repository call fails,
     * receives the error code set by the repository implementation;
     * left untouched on success.
     * @return `true` if the repository accepted the delete; `false`
     * if the repository returned `false` (e.g. `not_found` or a
     * backend failure).
     */
    bool Delete(const std::string& short_code, RepoError* error = nullptr);

private:
    /**
     * @brief Derives a status code from `result` and emits a
     * `LinkAccessEvent` via the analytics sink.
     *
     * Status-code mapping: `redirect` → 302, `not_found` → 404,
     * `inactive` / `expired` → 410. Failures from the analytics
     * sink are swallowed.
     * @param short_code Slug associated with the event.
     * @param result Resolution outcome used to derive the status
     * code and cache-hit flag for the event.
     */
    void EmitAnalytics(const std::string& short_code,
                       const ResolveResult& result);

    ///< Authoritative metadata repository; not owned, must outlive
    ///< this object.
    IMetadataRepository& repo_;

    ///< Cache-aside store; not owned, must outlive this object.
    ICacheStore& cache_;

    ///< Analytics event sink; not owned, must outlive this object.
    IAnalyticsSink& analytics_;

    ///< Time source for evaluating link and cache expiry; not
    ///< owned, must outlive this object.
    const IClock& clock_;
};
