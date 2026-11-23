#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/i_rate_limiter.hpp"

struct redisContext;

/**
 * @brief Connection and behavior settings for the `RedisRateLimiter` adapter.
 */
struct RedisRateLimiterConfig
{
    ///< Redis server hostname or IP address.
    std::string host = "127.0.0.1";

    ///< Redis server port.
    int port = 6379;

    ///< Namespace segment placed between `key_prefix` and the caller's key
    ///< in `ComposeKey`; allows multiple rate-limit policies to share one
    ///< Redis instance without key collisions.
    std::string scope = "global";

    ///< String prepended to every rate-limit key via `ComposeKey`; the
    ///< resulting key format is `<key_prefix><scope>:<id>`.
    std::string key_prefix = "ratelimit:";

    ///< Timeout passed to `redisConnectWithTimeout`.
    std::chrono::milliseconds connect_timeout {500};

    ///< Timeout set via `redisSetTimeout` after a successful connect;
    ///< controls how long each `EVAL` command may block.
    std::chrono::milliseconds command_timeout {500};
};

/**
 * @brief Non-authoritative, fail-open `IRateLimiter` implementation backed
 * by Redis via `hiredis`, using a fixed-window counter stored in an atomic
 * Lua script (`EVAL INCR + EXPIRE + TTL`). The connection is established
 * lazily on the first `Allow` call.
 *
 * Boundary policy: exactly `limit` requests per window are allowed; request
 * `limit + 1` is denied. When `limit == 0` all requests are denied.
 *
 * Failure behavior: connection failures and malformed Redis replies cause
 * `Allow` to return `{false, 0, now}` with `RateLimitError::unavailable`.
 * The caller is responsible for applying its own fail-open policy.
 *
 * Thread-safety: none. This class owns a single `redisContext*` with no lock.
 * Create a separate `RedisRateLimiter` per thread or serialize externally.
 */
class RedisRateLimiter final : public IRateLimiter
{
public:
    /**
     * @brief Stores the configuration; does not connect to Redis.
     * @param config Connection and scoping settings; the connection is
     * opened lazily on the first `Allow` call.
     */
    explicit RedisRateLimiter(RedisRateLimiterConfig config);

    /**
     * @brief Frees the `hiredis` connection context if one was opened.
     */
    ~RedisRateLimiter() override;

    /**
     * @brief Evaluates fixed-window allowance via an atomic Lua script.
     *
     * Atomically increments a Redis counter keyed by
     * `ComposeKey(config_.key_prefix, config_.scope, key)`, sets the
     * window expiry on the first request of a new window, then returns
     * whether the counter is within `limit`.
     *
     * @param key Caller identifier (e.g. client IP or API token hash);
     * must be non-empty.
     * @param limit Maximum requests allowed per `window`. A value of `0`
     * denies all requests.
     * @param window Fixed-window duration; must be positive.
     * @param[out] error If non-null and the call fails, receives
     * `RateLimitError::invalid_key` if `key` is empty,
     * `RateLimitError::invalid_policy` if `window <= 0`, or
     * `RateLimitError::unavailable` on connection failure or malformed
     * Redis reply; set to `RateLimitError::none` on a successful
     * evaluation (regardless of whether the request was allowed).
     * @return A `RateLimitDecision` with `allowed = true` and the
     * remaining request budget if the counter is within `limit`; a
     * decision with `allowed = false` on any error or when the limit is
     * exceeded. `reset_at` is set to `now + remaining TTL` on success,
     * or `now` on error.
     */
    RateLimitDecision Allow(const std::string& key,
                            uint64_t limit,
                            std::chrono::seconds window,
                            RateLimitError* error = nullptr) override;

    /**
     * @brief Builds the Redis storage key from its components.
     * @param prefix Key namespace prefix (e.g. `"ratelimit:"`).
     * @param scope Policy scope segment (e.g. `"global"`).
     * @param id Per-caller identifier (e.g. a client IP).
     * @return Concatenated key: `<prefix><scope>:<id>`.
     */
    static std::string ComposeKey(const std::string& prefix,
                                  const std::string& scope,
                                  const std::string& id);

private:
    /**
     * @brief Ensures `ctx_` is connected and in a non-error state,
     * reconnecting if necessary.
     * @param[out] error Set to `RateLimitError::unavailable` on failure;
     * `RateLimitError::none` on success.
     * @return `true` if `ctx_` is usable after the call; `false` otherwise.
     */
    bool EnsureConnected(RateLimitError* error);

    ///< Configuration copy; `host`, `port`, `scope`, `key_prefix`,
    ///< `connect_timeout`, and `command_timeout` are read on each call.
    RedisRateLimiterConfig config_;

    ///< `hiredis` connection context; `nullptr` before the first successful
    ///< connect. Freed in the destructor. Not protected by a mutex — this
    ///< class is not thread-safe.
    redisContext* ctx_ = nullptr;
};
