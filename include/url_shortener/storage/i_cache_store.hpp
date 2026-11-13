#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "url_shortener/storage/errors/cache_error.hpp"
#include "url_shortener/storage/models/cache_value.hpp"

/**
 * @brief Cache-aside store for short-link redirect data (see
 * `CLAUDE.md` "Storage: ports and adapters" and
 * `docs/storage/overview.md` for the full cache-aside contract).
 *
 * The service layer reads via `Get` first; on a miss or any failure
 * it falls through to `IMetadataRepository` and then calls `Set`
 * best-effort to populate the cache. `Delete`/`ClearByPrefix` are
 * called after `UpdateLink`/`DeleteLink` to invalidate stale entries.
 * Cache failures must never fail a request (fail-open).
 *
 * Thread-safety: implementations must be safe to call concurrently
 * from multiple request-handling threads.
 */
class ICacheStore
{
public:
    /**
     * @brief Virtual destructor; implementations may be destroyed
     * through a base `ICacheStore*`.
     */
    virtual ~ICacheStore() = default;

    /**
     * @brief Looks up a cache entry by key.
     * @param key Cache key to look up.
     * @param[out] error If non-null and a backend error occurs (not
     * a plain cache miss), receives the failure category; left
     * untouched on success and on a cache miss.
     * @return The cached `CacheValue` if the key exists and has not
     * expired; `std::nullopt` on a cache miss, TTL expiry, or
     * backend error.
     */
    virtual std::optional<CacheValue> Get(const std::string& key,
                                          CacheError* error = nullptr) = 0;

    /**
     * @brief Writes or overwrites a cache entry.
     * @param key Cache key to write.
     * @param value Payload to store.
     * @param ttl Optional time-to-live for the entry. A zero or
     * negative value deletes the key (equivalent to `Delete`). When
     * absent, the entry does not expire.
     * @param[out] error If non-null and the write fails, receives the
     * failure category; left untouched on success.
     * @return `true` if the entry was stored (or deleted due to a
     * non-positive TTL); `false` if the backend rejected the write.
     */
    virtual bool Set(const std::string& key,
                     const CacheValue& value,
                     std::optional<std::chrono::seconds> ttl,
                     CacheError* error = nullptr) = 0;

    /**
     * @brief Removes a single cache entry by key. Idempotent: a
     * no-op when the key does not exist.
     * @param key Cache key to remove.
     * @param[out] error If non-null and the removal fails, receives
     * the failure category; left untouched on success (including
     * when the key was absent).
     * @return `true` on success (including when the key did not
     * exist); `false` if the backend failed.
     */
    virtual bool Delete(const std::string& key,
                        CacheError* error = nullptr) = 0;

    /**
     * @brief Removes all cache entries whose key begins with
     * `prefix`.
     * @param prefix Key prefix to match; entries whose key starts
     * with this exact string are removed.
     * @param[out] error If non-null and the operation fails, receives
     * the failure category; left untouched on success.
     * @return `true` if the scan-and-delete completed (including
     * deleting zero entries); `false` if the backend failed.
     */
    virtual bool ClearByPrefix(const std::string& prefix,
                               CacheError* error = nullptr) = 0;
};
