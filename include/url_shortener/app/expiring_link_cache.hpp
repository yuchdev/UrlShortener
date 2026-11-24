#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief Process-local hot-path cache of full `Link` objects with a
 * TTL and a size bound, driven by an injected `IClock`.
 *
 * This is the L1 cache the repository-backed store (subtask 03) uses
 * in front of `IMetadataRepository`. Unlike the legacy
 * `InMemoryLinkCacheStore` (which never expires entries), entries
 * here expire lazily `ttl` after they were stored, so a link mutated
 * by another process (CLI, TUI) sharing the same backend stops being
 * served stale within one TTL rather than forever. A `ttl` of zero
 * means entries never expire; this preserves the single-process
 * in-memory backend behavior where the cache and the authoritative
 * store are the same process and no cross-process staleness exists.
 *
 * Thread-safety: all public methods are safe to call concurrently;
 * every operation takes a single short lock on `m_mutex_`, holds it
 * only for a hash-map lookup (and, for `get`, a copy of the `Link`
 * out), performs no I/O, and spawns no thread. `get`, `erase` and
 * `patch` are O(1) average; `set` is O(1) amortized, doing bounded
 * constant work at the size bound — on overflow it samples at most
 * `k_eviction_sample_size` entries to pick a victim rather than
 * sweeping the whole map, so an insert on the redirect path can never
 * degrade to O(n) (plan C10, CWE-400).
 *
 * Lifetime: the `IClock` is held by reference (same convention as
 * `LinkService` / `InMemoryCacheStore`); the caller must keep the
 * referenced clock alive for the entire lifetime of this object.
 * This type does not take ownership of the clock.
 *
 * Privacy: stores and logs nothing but the `Link` values passed to
 * `set` / mutated by `patch`; it keeps no slug history, no access
 * log, and no client identifiers.
 */
class ExpiringLinkCache final : public url_shortener::ILinkCacheStore
{
public:
    /**
     * @brief Maximum number of entries `set` inspects when choosing an
     * eviction victim at the size bound.
     *
     * Bounds the per-insert work to a constant: on overflow `set`
     * examines at most this many entries, evicts the first expired one
     * it finds, and otherwise evicts the first entry it looked at. This
     * keeps the redirect-path insert O(1) amortized instead of sweeping
     * the entire map (plan C10, CWE-400). It is exposed so tests can
     * reason about the sampling bound relative to `max_entries`.
     */
    static constexpr std::size_t k_eviction_sample_size = 8;

    /**
     * @brief Constructs an empty cache bound to `clock`.
     * @param clock Clock used to timestamp entries on `set` and to
     * evaluate expiry on `get` / `patch`. Must outlive this object;
     * not owned.
     * @param ttl Time-to-live applied to each entry, measured from
     * its `set` time. A value of zero disables expiry entirely
     * (entries live until evicted, erased, or cleared).
     * @param max_entries Upper bound on the number of cached entries,
     * strictly enforced: a `set` that would exceed it first evicts one
     * entry, preferring an expired entry among the first
     * `k_eviction_sample_size` examined and otherwise dropping the
     * first one examined. Protects memory only; it is not an LRU.
     */
    ExpiringLinkCache(const IClock& clock,
                      std::chrono::seconds ttl,
                      std::size_t max_entries = 10000);

    /**
     * @brief Looks up a cached link by slug, applying lazy expiry.
     *
     * If the entry exists but has reached its TTL — or its stored-at
     * time is in the future relative to the clock, which a backward
     * clock step can produce (see `is_expired`) — it is removed and
     * treated as a miss.
     * @param slug Slug to look up.
     * @return A copy of the cached `Link` if present and not expired;
     * `std::nullopt` on a miss or an expired (now removed) entry.
     */
    std::optional<Link> get(const std::string& slug) override;

    /**
     * @brief Stores or overwrites the entry for `slug`, resetting its
     * TTL anchor to the clock's current time.
     *
     * When the cache is at its size bound and `slug` is new, one entry
     * is evicted before inserting: `set` examines at most
     * `k_eviction_sample_size` entries and drops the first expired one
     * it finds, falling back to the first entry it examined when none
     * of the sampled entries is expired. The work is bounded by a
     * constant regardless of cache size.
     * @param slug Cache key.
     * @param link Link value to cache (copied in).
     */
    void set(const std::string& slug, const Link& link) override;

    /**
     * @brief Evicts a slug from the cache. Idempotent: a no-op when
     * the slug is absent.
     * @param slug Cache key to evict.
     */
    void erase(const std::string& slug) override;

    /**
     * @brief Mutates a cached `Link` in place without resetting its
     * TTL anchor (amended 2026-11-14, threat model T-32).
     *
     * Under the cache lock, looks `slug` up using the same lazy-expiry
     * rule as `get`. If the entry is present and live, invokes
     * `mutator` on the stored `Link` and returns `true`, leaving the
     * entry's stored-at time unchanged so the TTL stays anchored to
     * the original load time. This is used by `recordRedirect`
     * (subtask 05) for the in-memory counter bump: using `set` there
     * would refresh the TTL on every redirect, so a continuously hit
     * but cross-process disabled/deleted link would keep redirecting
     * indefinitely instead of expiring within one TTL.
     *
     * `patch` never inserts and never resurrects: on a miss or an
     * expired entry it does nothing and returns `false`.
     * @param slug Cache key to mutate.
     * @param mutator Callback invoked with a mutable reference to the
     * stored `Link`; called exactly once, only when the entry is
     * present and live, while the cache lock is held. It must not call
     * back into this cache (the lock is non-reentrant — doing so
     * deadlocks) and must not throw: an exception thrown by `mutator`
     * propagates out of `patch`, the lock is released by stack
     * unwinding, and the entry is left in whatever partially-mutated
     * state the callback reached. Not retained after the call.
     * @return `true` if an entry was present, live, and mutated;
     * `false` if the slug was absent or expired (in which case the
     * cache is left unchanged and nothing is inserted).
     */
    bool patch(const std::string& slug,
               const std::function<void(Link&)>& mutator) override;

    /**
     * @brief Removes all cached entries.
     */
    void clear();

    /**
     * @brief Returns the current number of entries, including any that
     * have expired but not yet been lazily removed.
     *
     * Intended for tests and metrics.
     * @return The size of the backing map at the moment of the call.
     */
    std::size_t size() const;

private:
    /**
     * @brief A cached `Link` together with the time it was stored,
     * used to evaluate TTL expiry.
     */
    struct entry
    {
        ///< The cached link value.
        Link link;

        ///< Clock time at which this entry was stored via `set`;
        ///< the TTL is measured from this instant and `patch` leaves
        ///< it unchanged.
        std::chrono::system_clock::time_point stored_at;
    };

    /**
     * @brief Reports whether `e` has reached its TTL relative to
     * `now`, fail-safe against a backward clock step.
     *
     * The injected clock is a wall clock, which can jump backwards
     * (NTP correction, VM resume). Were expiry computed purely as
     * `now - e.stored_at >= m_ttl_`, such a jump would make the
     * difference small or negative and silently extend every entry's
     * lifetime by the size of the jump, reopening the cross-process
     * staleness window this cache exists to bound (threat model T-32).
     * To make a clock anomaly only ever shorten a lifetime and never
     * extend it, an entry whose `stored_at` is in the future relative
     * to `now` (`now < e.stored_at`) is treated as expired.
     * @param e Entry to test.
     * @param now Current clock time.
     * @return `false` when `m_ttl_` is zero (expiry disabled);
     * `true` when `now < e.stored_at` (stored in the future — a
     * backward clock step, treated as expired) or once
     * `now - e.stored_at >= m_ttl_`; `false` otherwise.
     */
    bool is_expired(const entry& e,
                    std::chrono::system_clock::time_point now) const;

    ///< Clock used for timestamps and expiry; not owned, must
    ///< outlive this object.
    const IClock& m_clock_;

    ///< Entry time-to-live; zero disables expiry.
    std::chrono::seconds m_ttl_;

    ///< Maximum number of entries retained; a soft memory bound,
    ///< not an LRU.
    std::size_t m_max_entries_;

    ///< Guards `m_entries_`; held briefly by every public method.
    mutable std::mutex m_mutex_;

    ///< Slug -> entry cache map.
    std::unordered_map<std::string, entry> m_entries_;
};

}  // namespace app
}  // namespace url_shortener
