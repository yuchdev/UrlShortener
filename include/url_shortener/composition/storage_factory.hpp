#pragma once
#include <memory>

#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/i_analytics_sink.hpp"
#include "url_shortener/storage/i_cache_store.hpp"
#include "url_shortener/storage/i_metadata_repository.hpp"
#include "url_shortener/storage/i_rate_limiter.hpp"

/**
 * @brief Fully-wired set of storage port adapters ready for injection
 * into the application.
 *
 * Produced by `BuildStorageAdapters`. All members are shared pointers
 * owned by this struct; callers may keep individual adapters alive
 * independently by retaining a copy of the shared pointer.
 *
 * @note `rate_limiter` is `nullptr` when
 * `StorageConfig::rate_limit.enabled` is `false`; callers must check
 * before dereferencing it.
 */
struct StorageAdapters
{
    ///< Metadata (link-record) storage adapter; never null.
    std::shared_ptr<IMetadataRepository> metadata;

    ///< Cache-aside adapter; never null (a no-op implementation is
    ///< used when `StorageConfig::cache.backend == "none"`).
    std::shared_ptr<ICacheStore> cache;

    ///< Analytics event sink; never null (a no-op implementation
    ///< is used when
    ///< `StorageConfig::analytics.backend == "noop"`).
    std::shared_ptr<IAnalyticsSink> analytics;

    ///< Rate-limiter adapter; `nullptr` when rate limiting is
    ///< disabled (`StorageConfig::rate_limit.enabled == false`).
    std::shared_ptr<IRateLimiter> rate_limiter;
};

/**
 * @brief Constructs runtime storage adapters from a validated
 * `StorageConfig`.
 *
 * Selects and instantiates the concrete implementation for each of
 * the four storage ports based on the backend strings in `config`.
 * The `clock` parameter is forwarded to adapters that need a time
 * source (in-memory cache TTL, in-memory rate limiter).
 *
 * No-op adapters are returned for disabled backends (cache `"none"`,
 * analytics `"noop"`); `rate_limiter` is set to `nullptr` when
 * `config.rate_limit.enabled` is `false`.
 * @param config Validated storage configuration produced by
 * `ParseStorageConfigFile` or `ParseStorageConfigYaml`.
 * @param clock Clock used by time-aware in-memory adapters; must
 * outlive the returned adapters.
 * @return A fully-populated `StorageAdapters` bundle.
 */
StorageAdapters BuildStorageAdapters(const StorageConfig& config,
                                     const IClock& clock);
