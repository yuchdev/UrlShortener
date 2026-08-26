#pragma once
#include <memory>

#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/i_analytics_sink.hpp"
#include "url_shortener/storage/i_cache_store.hpp"
#include "url_shortener/storage/i_metadata_repository.hpp"
#include "url_shortener/storage/i_rate_limiter.hpp"

struct StorageAdapters {
    std::shared_ptr<IMetadataRepository> metadata;
    std::shared_ptr<ICacheStore> cache;
    std::shared_ptr<IAnalyticsSink> analytics;
    std::shared_ptr<IRateLimiter> rate_limiter;
};

/** Build runtime storage adapters from validated storage configuration. */
StorageAdapters BuildStorageAdapters(const StorageConfig& config, const IClock& clock);
