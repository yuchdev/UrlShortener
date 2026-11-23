#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/i_cache_store.hpp"

struct redisContext;

/**
 * @brief Connection and behavior settings for the `RedisCacheStore`
 * adapter.
 */
struct RedisCacheConfig
{
    ///< Redis server hostname or IP address.
    std::string host = "127.0.0.1";

    ///< Redis server port.
    int port = 6379;

    ///< String prepended to every key passed to Redis commands via
    ///< `BuildCacheKey(key_prefix, key)`. Allows multiple logical caches
    ///< to share one Redis instance without key collisions.
    std::string key_prefix = "link:";

    ///< TTL applied by `Set` when the caller passes `std::nullopt` for
    ///< `ttl`; `std::nullopt` here means keys are stored without
    ///< expiration unless the caller supplies an explicit TTL.
    std::optional<std::chrono::seconds> default_ttl = std::nullopt;

    ///< Declared for configuration compatibility; not currently read by
    ///< any method in `RedisCacheStore`. Connection failures always cause
    ///< methods to return `CacheError::unavailable` regardless of this
    ///< setting — the fail-open behavior is the responsibility of the
    ///< cache-aside caller (e.g. `LinkService`).
    bool fail_open = true;

    ///< Timeout passed to `redisConnectWithTimeout`; controls how long
    ///< the adapter waits for the initial TCP connection to Redis.
    std::chrono::milliseconds connect_timeout {500};

    ///< Timeout set via `redisSetTimeout` after a successful connect;
    ///< controls how long each individual Redis command may take.
    std::chrono::milliseconds command_timeout {500};
};

/**
 * @brief Non-authoritative, fail-open `ICacheStore` implementation backed
 * by Redis via `hiredis`. The connection is established lazily on the first
 * operation and re-established automatically if a previous connection was
 * found in an error state.
 *
 * Failure behavior: all public methods return `std::nullopt` / `false` with
 * `CacheError::unavailable` on connection or command failure. The cache-aside
 * layer (`LinkService`) treats these as cache misses — cache failures never
 * propagate as request failures.
 *
 * Thread-safety: none. This class owns a single `redisContext*` and does not
 * protect it with a lock. Create a separate `RedisCacheStore` per thread or
 * serialize access externally.
 *
 * @note `ClearByPrefix` uses `KEYS <prefix>*` and is not safe for large
 * keyspaces; use it only in management-plane operations, never on a hot path.
 */
class RedisCacheStore final : public ICacheStore
{
public:
    /**
     * @brief Stores the configuration; does not connect to Redis.
     * @param config Connection settings; the connection is opened lazily
     * on the first call to `Get`, `Set`, `Delete`, or `ClearByPrefix`.
     */
    explicit RedisCacheStore(RedisCacheConfig config);

    /**
     * @brief Frees the `hiredis` connection context if one was opened.
     */
    ~RedisCacheStore() override;

    /**
     * @brief Fetches a cached value by key.
     * @param key Short-code or other lookup key; `key_prefix` is prepended
     * before the Redis command.
     * @param[out] error If non-null and the call fails, receives
     * `CacheError::unavailable` on connection or command failure; set to
     * `CacheError::none` on success (including a cache miss).
     * @return The deserialized `CacheValue` on a cache hit; `std::nullopt`
     * on a cache miss, deserialization failure, or any Redis error.
     */
    std::optional<CacheValue> Get(const std::string& key,
                                  CacheError* error = nullptr) override;

    /**
     * @brief Writes a value to the cache.
     * @param key Lookup key; `key_prefix` is prepended.
     * @param value Value to serialize and store.
     * @param ttl If set and positive, the key is stored with `SETEX` using
     * this TTL; if `std::nullopt`, `config_.default_ttl` is used instead;
     * if both are absent or zero, the key is stored without expiry via
     * `SET`.
     * @param[out] error If non-null and the call fails, receives
     * `CacheError::unavailable` on connection failure or
     * `CacheError::permanent_failure` if the Redis reply was not a status
     * response; set to `CacheError::none` on success.
     * @return `true` if Redis returned a status reply; `false` otherwise.
     */
    bool Set(const std::string& key,
             const CacheValue& value,
             std::optional<std::chrono::seconds> ttl,
             CacheError* error = nullptr) override;

    /**
     * @brief Removes a single key from the cache.
     * @param key Lookup key; `key_prefix` is prepended.
     * @param[out] error If non-null and the call fails, receives
     * `CacheError::unavailable`; set to `CacheError::none` on success
     * (even if the key did not exist).
     * @return `true` if the `DEL` command executed without error; `false`
     * on connection or command failure.
     */
    bool Delete(const std::string& key, CacheError* error = nullptr) override;

    /**
     * @brief Removes all keys matching `<key_prefix><prefix>*` via
     * `KEYS` + `DEL`. Not safe for large keyspaces.
     * @param prefix Suffix appended to `key_prefix` to form the scan
     * pattern.
     * @param[out] error If non-null and the call fails, receives
     * `CacheError::unavailable`; set to `CacheError::none` on success.
     * @return `true` if the `KEYS` command returned an array and all `DEL`
     * commands were dispatched; `false` on connection failure or if the
     * `KEYS` reply was not an array.
     */
    bool ClearByPrefix(const std::string& prefix,
                       CacheError* error = nullptr) override;

private:
    /**
     * @brief Ensures `ctx_` is connected and in a non-error state,
     * reconnecting if necessary.
     * @param[out] error Set to `CacheError::unavailable` on failure;
     * `CacheError::none` on success.
     * @return `true` if `ctx_` is usable after the call; `false` otherwise.
     */
    bool EnsureConnected(CacheError* error);

    /**
     * @brief Returns `CacheError::unavailable` unconditionally.
     * The `connected` parameter is accepted for signature uniformity but
     * is not currently used.
     */
    static CacheError MapRedisError(bool connected);

    /**
     * @brief Returns `ttl` if set, otherwise `config_.default_ttl`.
     * @param ttl Per-call TTL override.
     * @return Effective TTL to use for a `Set` operation.
     */
    std::optional<std::chrono::seconds> EffectiveTtl(
        std::optional<std::chrono::seconds> ttl) const;

    /**
     * @brief Prepends `config_.key_prefix` to `key` via
     * `redis_cache::BuildCacheKey`.
     */
    std::string FullKey(const std::string& key) const;

    /**
     * @brief Prepends `config_.key_prefix` to `prefix` via
     * `redis_cache::BuildCacheKey`, producing the scan pattern base for
     * `ClearByPrefix`.
     */
    std::string FullPrefix(const std::string& prefix) const;

    ///< Configuration copy; `host`, `port`, `key_prefix`, `default_ttl`,
    ///< `connect_timeout`, and `command_timeout` are read on each operation.
    RedisCacheConfig config_;

    ///< `hiredis` connection context; `nullptr` before the first successful
    ///< connect. Freed in the destructor. Not protected by a mutex — this
    ///< class is not thread-safe.
    redisContext* ctx_ = nullptr;
};
