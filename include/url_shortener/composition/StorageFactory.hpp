#pragma once
#include <memory>

#include "url_shortener/config/StorageConfig.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/IAnalyticsSink.hpp"
#include "url_shortener/storage/ICacheStore.hpp"
#include "url_shortener/storage/IMetadataRepository.hpp"
#include "url_shortener/storage/IRateLimiter.hpp"

struct StorageAdapters {
    std::shared_ptr<IMetadataRepository> metadata;
    std::shared_ptr<ICacheStore> cache;
    std::shared_ptr<IAnalyticsSink> analytics;
    std::shared_ptr<IRateLimiter> rate_limiter;
};

/** Build runtime storage adapters from validated storage configuration. */
StorageAdapters BuildStorageAdapters(const StorageConfig& config, const IClock& clock);
