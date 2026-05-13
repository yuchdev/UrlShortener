#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/ICacheStore.hpp"

struct redisContext;

/**
 * @brief Connection/configuration settings for Redis cache adapter.
 */
struct RedisCacheConfig {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::string key_prefix = "link:";
    std::optional<std::chrono::seconds> default_ttl = std::nullopt;
    bool fail_open = true;
    std::chrono::milliseconds connect_timeout{500};
    std::chrono::milliseconds command_timeout{500};
};

/**
 * @brief Redis-backed implementation of ICacheStore.
 */
class RedisCacheStore final : public ICacheStore {
public:
    explicit RedisCacheStore(RedisCacheConfig config);
    ~RedisCacheStore() override;

    std::optional<CacheValue> Get(const std::string& key, CacheError* error = nullptr) override;
    bool Set(const std::string& key,
             const CacheValue& value,
             std::optional<std::chrono::seconds> ttl,
             CacheError* error = nullptr) override;
    bool Delete(const std::string& key, CacheError* error = nullptr) override;
    bool ClearByPrefix(const std::string& prefix, CacheError* error = nullptr) override;

private:
    bool EnsureConnected(CacheError* error);
    static CacheError MapRedisError(bool connected);
    std::optional<std::chrono::seconds> EffectiveTtl(std::optional<std::chrono::seconds> ttl) const;
    std::string FullKey(const std::string& key) const;
    std::string FullPrefix(const std::string& prefix) const;

    RedisCacheConfig config_;
    redisContext* ctx_ = nullptr;
};
