#pragma once

#include <optional>
#include <shared_mutex>
#include <unordered_map>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/i_cache_store.hpp"

/**
 * @brief Thread-safe in-memory `ICacheStore` with optional per-entry
 * TTL and lazy expiry eviction. Expired entries are removed on the
 * next `Get` call for that key rather than proactively.
 *
 * All public methods always succeed (they never return `false` or set
 * a non-`none` `CacheError`). `Get`, `Set`, `Delete`, and
 * `ClearByPrefix` acquire `mutex_` exclusively even for read-only
 * lookups that may evict an expired entry.
 *
 * Lifetime: does not own `clock`; the caller must keep it alive for
 * at least as long as this object exists.
 *
 * Thread-safety: all public methods are safe to call concurrently.
 *
 * @see ICacheStore
 */
class InMemoryCacheStore final : public ICacheStore
{
public:
    /**
     * @brief Stores a reference to `clock`; does not populate any
     * entries.
     * @param clock Clock used to evaluate TTL expiry; must outlive
     * this object.
     */
    explicit InMemoryCacheStore(const IClock& clock);

    /**
     * @brief Returns the value for `key` if it exists and has not
     * expired; lazily evicts the entry if its TTL has passed.
     * @param key Cache key to look up.
     * @param[out] error If non-null, always set to
     * `CacheError::none` (this implementation never fails).
     * @return The cached `CacheValue` on a live hit; `std::nullopt`
     * on a miss or TTL expiry.
     * @see ICacheStore::Get
     */
    std::optional<CacheValue> Get(const std::string& key,
                                  CacheError* error = nullptr) override;

    /**
     * @brief Writes or overwrites `key` with `value`. A zero or
     * negative `ttl` deletes the key (identical to calling
     * `Delete`); an absent `ttl` stores the entry without expiry.
     * @param key Cache key to write.
     * @param value Payload to store.
     * @param ttl Optional time-to-live; a non-positive value
     * removes the existing entry for `key`.
     * @param[out] error If non-null, always set to
     * `CacheError::none` (this implementation never fails).
     * @return Always `true`.
     * @see ICacheStore::Set
     */
    bool Set(const std::string& key,
             const CacheValue& value,
             std::optional<std::chrono::seconds> ttl,
             CacheError* error = nullptr) override;

    /**
     * @brief Removes the entry for `key`. Idempotent: a no-op when
     * the key does not exist.
     * @param key Cache key to remove.
     * @param[out] error If non-null, always set to
     * `CacheError::none`.
     * @return Always `true`.
     * @see ICacheStore::Delete
     */
    bool Delete(const std::string& key, CacheError* error = nullptr) override;

    /**
     * @brief Removes all entries whose key starts with `prefix`.
     * @param prefix Key prefix to match; entries with keys that
     * begin with this exact string are removed.
     * @param[out] error If non-null, always set to
     * `CacheError::none`.
     * @return Always `true`.
     * @see ICacheStore::ClearByPrefix
     */
    bool ClearByPrefix(const std::string& prefix,
                       CacheError* error = nullptr) override;

private:
    /**
     * @brief Internal storage cell combining the cached payload
     * with an optional wall-clock expiry time.
     */
    struct Entry
    {
        ///< The cached redirect payload.
        CacheValue value;

        ///< Wall-clock expiry set at `Set` time
        ///< (`clock_.now() + ttl`); absent when no TTL was given.
        std::optional<std::chrono::system_clock::time_point> expires_at;
    };

    ///< Clock used to evaluate TTL expiry at `Get` and `Set` time.
    ///< Not owned; must outlive this object.
    const IClock& clock_;

    ///< Guards `entries_`; `mutable` so that `Get` (non-const but
    ///< logically read-like) can acquire it via `unique_lock`.
    mutable std::shared_mutex mutex_;

    ///< Live cache entries, keyed by cache key string. Guarded by
    ///< `mutex_`.
    std::unordered_map<std::string, Entry> entries_;
};
