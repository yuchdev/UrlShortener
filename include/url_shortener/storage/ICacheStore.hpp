#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/errors/CacheError.hpp"
#include "url_shortener/storage/models/CacheValue.hpp"

/**
 * @brief Cache abstraction used by service layer for cache-aside reads.
 */
class ICacheStore {
public:
    virtual ~ICacheStore() = default;

    /**
     * @brief Reads a cache value by key.
     */
    virtual std::optional<CacheValue> Get(const std::string& key, CacheError* error = nullptr) = 0;

    /**
     * @brief Writes a cache value by key with an optional TTL.
     */
    virtual bool Set(const std::string& key,
                     const CacheValue& value,
                     std::optional<std::chrono::seconds> ttl,
                     CacheError* error = nullptr) = 0;

    /**
     * @brief Deletes a cache key.
     */
    virtual bool Delete(const std::string& key, CacheError* error = nullptr) = 0;

    /**
     * @brief Deletes all keys by key prefix.
     */
    virtual bool ClearByPrefix(const std::string& prefix, CacheError* error = nullptr) = 0;
};
