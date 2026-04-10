#pragma once

#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/ICacheStore.hpp"

/**
 * @brief Thread-safe in-memory cache with optional TTL and lazy eviction.
 */
class InMemoryCacheStore final : public ICacheStore {
public:
    explicit InMemoryCacheStore(const IClock& clock);

    std::optional<CacheValue> Get(const std::string& key, CacheError* error = nullptr) override;
    bool Set(const std::string& key,
             const CacheValue& value,
             std::optional<std::chrono::seconds> ttl,
             CacheError* error = nullptr) override;
    bool Delete(const std::string& key, CacheError* error = nullptr) override;
    bool ClearByPrefix(const std::string& prefix, CacheError* error = nullptr) override;

private:
    struct Entry {
        CacheValue value;
        std::optional<std::chrono::system_clock::time_point> expires_at;
    };

    const IClock& clock_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
};
