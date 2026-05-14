#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/IRateLimiter.hpp"

struct redisContext;

/**
 * @brief Redis connection/configuration for rate limiting adapter.
 */
struct RedisRateLimiterConfig {
    std::string host = "127.0.0.1";
    int port = 6379;
    std::string scope = "global";
    std::string key_prefix = "ratelimit:";
    std::chrono::milliseconds connect_timeout{500};
    std::chrono::milliseconds command_timeout{500};
};

/**
 * @brief Redis-backed fixed-window rate limiter.
 *
 * Boundary policy: exactly `limit` requests per window are allowed.
 * Request `limit + 1` is denied.
 */
class RedisRateLimiter final : public IRateLimiter {
public:
    explicit RedisRateLimiter(RedisRateLimiterConfig config);
    ~RedisRateLimiter() override;

    /**
     * @brief Evaluates fixed-window allowance with atomic Lua script.
     */
    RateLimitDecision Allow(const std::string& key,
                            uint64_t limit,
                            std::chrono::seconds window,
                            RateLimitError* error = nullptr) override;

    /**
     * @brief Builds stable storage key: `ratelimit:{scope}:{id}`.
     */
    static std::string ComposeKey(const std::string& prefix,
                                  const std::string& scope,
                                  const std::string& id);

private:
    bool EnsureConnected(RateLimitError* error);

    RedisRateLimiterConfig config_;
    redisContext* ctx_ = nullptr;
};
