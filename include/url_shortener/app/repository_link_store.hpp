#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/i_cache_store.hpp>
#include <url_shortener/storage/i_metadata_repository.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace url_shortener
{
namespace app
{

/**
 * @brief Tunable knobs for `RepositoryLinkStore`.
 *
 * Defaults match the values validated by the subtask-03 read tests and
 * the subtask-05 write-behind design.
 */
struct RepositoryLinkStoreOptions
{
    ///< Interval at which the subtask-05 background flusher persists
    ///< pending redirect-stat deltas. Unused by the read side
    ///< implemented in subtask 03; a value of zero selects
    ///< write-through in subtask 05. Carried here so the type is
    ///< stable across subtasks.
    std::chrono::seconds stats_flush_interval {30};

    ///< Wall-clock budget for the *final* (shutdown) stats flush run by
    ///< `stop_stats_flusher`/the destructor. The shutdown flush does one
    ///< repository session per pending slug, each of which may block on a
    ///< backend `busy_timeout`; to bound teardown latency the shutdown
    ///< flush stops once this budget elapses (measured on a *steady*
    ///< clock, never the injected `IClock`), drops the remaining pending
    ///< deltas (stats are best-effort, D12) and logs one coalesced warn.
    ///< Does **not** apply to the periodic flusher-thread flushes, which
    ///< run to completion. The 5 s default is a safety bound; tests set a
    ///< tiny value to exercise the drop path deterministically.
    std::chrono::milliseconds shutdown_flush_budget {std::chrono::seconds(5)};
};

/**
 * @brief `ILinkStore` over the pluggable `::IMetadataRepository` /
 * `::ICacheStore` ports, with a process-local full-`Link` L1 cache in
 * front of the repository.
 *
 * This is the store installed when the server/CLI runs with
 * `--storage-config` (Task 02.0 README decision 1). It replaces neither
 * the legacy `LegacyLinkStore` nor the global singletons it wraps;
 * both coexist until the bootstrap (subtask 11) selects one.
 *
 * Read model (subtask 03):
 * - `findBySlug` consults the L1 cache (`m_l1_`) first, then the
 *   repository on a miss, decoding the `::LinkRecord` through the
 *   subtask-02 codec and populating L1 on success. The configured
 *   `::ICacheStore` is *never read* for link data — only invalidated
 *   (threat model T-38); the full `Link` cannot be represented by
 *   `CacheValue`, and a poisoned shared cache must never be able to
 *   answer a redirect.
 * - `findById` issues a single indexed `::IMetadataRepository::GetById`
 *   lookup (O(1) in-memory, O(log n) on SQL backends), decodes the
 *   record, and populates L1 on success (admin paths only — never the
 *   redirect fast path). The former bounded `ListLinks` scan (threat
 *   model T-36) is removed.
 * - Repository failures are logged with the error name only and
 *   surface as `std::nullopt`; an outage therefore looks like
 *   "not found" (accepted limitation R-5 / threat model T-35).
 *
 * Write model (subtask 04): `create` maps the link to a
 * `::CreateLinkRequest` through the codec and calls
 * `::IMetadataRepository::CreateLink`; `update` maps to a full
 * `::LinkRecord` and calls `UpdateLink`. Both translate `::RepoError`
 * to `AppErrorCode` with the exact messages the legacy store uses
 * (so REST/CLI output is unchanged, contracts C3/C4) and invalidate
 * both cache tiers best-effort (L1 always, the configured
 * `::ICacheStore` on success), never failing a request on a cache
 * error. Validation (SSRF/URL/slug) stays upstream in
 * `LinkCommandService`; these methods are persistence only (threat
 * model T-40).
 *
 * Redirect-stat write-behind (subtask 05): `recordRedirect` adds **no
 * repository I/O to a redirect request** (plan C10). On the request
 * thread it only accumulates a per-slug delta under `m_stats_mutex_`
 * and patches the L1 copy in place (`m_l1_->patch`, never `set`, so a
 * hot link's TTL is not refreshed — threat model T-32). Every record
 * loaded from the repository has its still-pending delta merged back in
 * (merge-on-load), so an L1 TTL expiry or size eviction never loses or
 * regresses counts. A background flusher thread persists the coalesced
 * deltas on `m_options_.stats_flush_interval`, reading each record fresh
 * and rewriting only its `lnk_stats` attribute (via `set_stats_attribute`
 * over `get_stats_attribute`), so a flush can never revert a concurrent
 * `disable`/`delete`/field change (except race R-1, documented on
 * `flush_stats`). With `stats_flush_interval == 0` no thread runs and
 * `recordRedirect` writes through synchronously from the caller's thread
 * (in-memory backend only, subtask 11).
 *
 * Privacy: logs only the slug and backend error names. Decoded
 * `target_url`/tag/metadata/campaign bytes are untrusted (a foreign
 * process may have written the record) and are never logged (threat
 * model T-37; `record_to_link` warning). The flusher logs only the slug
 * and backend error name on a failed persist.
 *
 * Thread-safety: the read/write ports (`create`, `findBySlug`,
 * `findById`, `slugExists`, `update`, `invalidateCache`) are safe to
 * call concurrently; they forward through the thread-safe collaborators
 * (`m_repo_`, `m_l1_`, `m_cache_`). `recordRedirect`, merge-on-load, and
 * `flush_stats` coordinate on `m_stats_mutex_`, which guards `m_pending_`
 * only and is never held across repository I/O. The flusher thread is
 * the only thread that calls the repository from inside `flush_stats`
 * during normal operation; the request threads never do.
 *
 * Two mutexes, with a strict lock order:
 * 1. `m_write_mutex_` (coarse "store write" lock) is held by `create`,
 *    `update`, and by `flush_stats` **around each slug's**
 *    read-modify-write (per slug, released between slugs so a long flush
 *    never blocks writers for long). It serializes the flusher's
 *    `GetByShortCode -> patch lnk_stats -> UpdateLink` against a
 *    `create`/`update` of the same slug, so an in-process `disable`/
 *    soft-delete committed by `update` can no longer be reverted by a
 *    concurrent flush that read the record just before (the former R-1
 *    window is now closed *within one process*). `recordRedirect` and
 *    the read paths (`findBySlug`/`findById`/`merge_pending_stats`) do
 *    **not** take `m_write_mutex_`: the redirect fast path stays O(1)
 *    with no new lock (C10), and reads remain independent of writers
 *    (last-writer-wins on the repository).
 * 2. `m_stats_mutex_` (fine "pending map" lock) guards `m_pending_` only.
 *
 * Lock order is `m_write_mutex_` **then** `m_stats_mutex_`, and never the
 * reverse: the only nesting is `flush_stats`/`create`/`update` holding
 * `m_write_mutex_` and then briefly taking `m_stats_mutex_` (via the swap
 * / `requeue_delta` / `merge`). `m_stats_mutex_` is never held while
 * taking `m_write_mutex_` and never held across repository I/O. This
 * makes the pair deadlock-free. The *cross-process* lost-update window
 * (two processes flushing the same slug) is unaffected and remains
 * accepted per decision-log D12 (amended) — the write mutex is
 * process-local only.
 *
 * Lifecycle (thread ownership): the flusher thread is **not** started by
 * the constructor. The composition root calls `start_stats_flusher()`
 * after wiring and `stop_stats_flusher()` before teardown; the
 * destructor calls `stop_stats_flusher()` as a safety net (idempotent).
 * `start_stats_flusher()` / `stop_stats_flusher()` are not themselves
 * thread-safe against each other and are meant to be driven from the
 * owning (composition/shutdown) thread only. `flush_stats()` is safe to
 * call from that thread at any time (tests drive flushes explicitly
 * rather than sleeping for the timer). Destroying the object while
 * another thread is still calling `recordRedirect` is undefined, as with
 * any store.
 *
 * Lifetime: owns shared references to `repo`, `l1`, and (optionally)
 * `cache` via `shared_ptr`, so they outlive this object by
 * construction. The `IClock` is held by reference and is *not* owned;
 * the caller must keep the referenced clock alive for the entire
 * lifetime of this object (same convention as `ExpiringLinkCache`).
 */
class RepositoryLinkStore final : public ILinkStore
{
public:
    /**
     * @brief Wires the store to its ports and clock.
     * @param repo Authoritative metadata repository; must not be null
     * (a null `repo` is a startup programming error and throws
     * `std::invalid_argument`). Shared ownership is taken.
     * @param cache Configured cache-aside store, invalidated
     * best-effort on writes and never read for link data; may be null
     * (no configured cache). Shared ownership is taken when present.
     * @param l1 Process-local full-`Link` hot-path cache (subtask 01);
     * must not be null (throws `std::invalid_argument`). Shared
     * ownership is taken.
     * @param clock Clock used by the store; held by reference and not
     * owned, so it must outlive this object.
     * @param options Scan/flush tuning; see `RepositoryLinkStoreOptions`.
     */
    RepositoryLinkStore(std::shared_ptr<::IMetadataRepository> repo,
                        std::shared_ptr<::ICacheStore> cache,
                        std::shared_ptr<url_shortener::ILinkCacheStore> l1,
                        const IClock& clock,
                        RepositoryLinkStoreOptions options = {});

    /**
     * @brief Stops the flusher thread (if running) and performs a final
     * `flush_stats()` so no pending delta is lost on teardown.
     *
     * Equivalent to calling `stop_stats_flusher()`; idempotent and safe
     * even if the flusher was never started. Must not run concurrently
     * with `recordRedirect` on another thread (see the class lifecycle
     * notes).
     */
    ~RepositoryLinkStore() override;

    RepositoryLinkStore(const RepositoryLinkStore&) = delete;
    RepositoryLinkStore& operator=(const RepositoryLinkStore&) = delete;
    RepositoryLinkStore(RepositoryLinkStore&&) = delete;
    RepositoryLinkStore& operator=(RepositoryLinkStore&&) = delete;

    /**
     * @brief Persists a new link, then invalidates both cache tiers.
     *
     * Write path, in order: (1) map the link to a `::CreateLinkRequest`
     * through `link_to_create_request` — a codec failure yields
     * `AppErrorCode::internal` ("invalid link record") and the
     * repository is not called; (2) parse `link.created_at` as the
     * creation timestamp — an unparseable value yields the same
     * `internal` failure with the repository not called; (3)
     * `::IMetadataRepository::CreateLink(request, link.id, now)`; (4)
     * on success, populate L1 with a fresh copy (`m_l1_->set`) and
     * `Delete(link.slug)` on the configured `::ICacheStore`
     * best-effort. Steps (3) and (4) run under `m_write_mutex_` so the
     * persist + invalidation is atomic with respect to a concurrent
     * `update`/`flush_stats` of the same slug (lock order: this is the
     * outer lock, see the class thread-safety notes); slug uniqueness is
     * still enforced atomically by the repository. The redirect fast path
     * never takes this lock.
     * @param link Fully-formed, already-validated link to store;
     * `link.id`, `link.slug`, and `link.created_at` must be set.
     * URL/SSRF/slug validation is the caller's responsibility
     * (`LinkCommandService`, threat model T-40); this method performs
     * none.
     * @param[out] error If non-null and the call fails, receives:
     * `internal` ("invalid link record") when the codec rejects the
     * link or `created_at` does not parse; `slug_conflict` ("slug
     * already in use") when the slug is already taken
     * (`RepoError::already_exists`); `invalid_field` ("invalid link")
     * on `RepoError::invalid_argument`; `storage_failure` ("repository
     * failure") on any other backend error. Untouched on success.
     * @return `true` if the link was persisted; `false` on any failure
     * above.
     */
    bool create(const Link& link, AppError* error = nullptr) override;

    /**
     * @brief Looks up a link by slug: L1 cache, then repository.
     *
     * Read path, in order: (1) `m_l1_->get(slug)` — a hit returns
     * immediately; (2) on a miss, `m_repo_->GetByShortCode(slug)` — an
     * absent code returns `std::nullopt` and caches nothing (negative
     * results are not cached); (3) a present record is decoded with
     * `record_to_link` and, on success, written to L1 before being
     * returned. The configured `::ICacheStore` is never consulted
     * (threat model T-38).
     * @param slug Short-code slug to look up.
     * @return The matching link; `std::nullopt` on an absent slug, a
     * repository error (logged as `link_store_read_failed`, error name
     * only), or an undecodable record (logged as
     * `link_store_decode_failed`, slug only). A repository error is
     * indistinguishable from "not found" to the caller (R-5).
     */
    std::optional<Link> findBySlug(const std::string& slug) const override;

    /**
     * @brief Looks up a link by its stable identifier via a single
     * indexed repository lookup.
     *
     * Calls `::IMetadataRepository::GetById` once (O(1) in-memory,
     * O(log n) on SQL backends thanks to the `idx_links_id` index),
     * which returns inactive and soft-deleted records too so an admin
     * lookup finds disabled/deleted links. A decoded match is written to
     * L1. Used only by admin paths (`link get --id`), never by redirects
     * (C10). The former bounded `ListLinks` scan (threat model T-36) is
     * removed.
     * @param id Stable link identifier to look up.
     * @return The matching link; `std::nullopt` if the id is empty, no
     * record matches, the record fails to decode (logged as
     * `link_store_decode_failed`, slug only), or the repository errors
     * (logged as `link_store_read_failed`, id and error name only).
     */
    std::optional<Link> findById(const std::string& id) const override;

    /**
     * @brief Reports whether `slug` is registered in the repository.
     * @param slug Short-code slug to check.
     * @return `true` if the repository reports the slug exists;
     * `false` if it does not or the backend errored. On a backend
     * error the error name is logged (`link_store_exists_failed`) and
     * `false` is returned — a subsequent `create` still fails on the
     * uniqueness constraint and maps to `slug_conflict`.
     */
    bool slugExists(const std::string& slug) const override;

    /**
     * @brief Drops any cached copy of `slug` from both cache tiers.
     *
     * Erases the L1 entry (`m_l1_->erase`) and, when a configured
     * `::ICacheStore` is present, calls `m_cache_->Delete(slug)`
     * best-effort, ignoring its result so a cache failure never fails
     * the request (cache-aside contract, CLAUDE.md).
     * @param slug Short-code slug whose cached copies to evict.
     */
    void invalidateCache(const std::string& slug) override;

    /**
     * @brief Persists a full-record update, then invalidates both
     * cache tiers.
     *
     * Write path, in order: (1) map the link to a `::LinkRecord`
     * through `link_to_record` — a codec failure yields
     * `AppErrorCode::internal` ("invalid link record") and the
     * repository is not called; (2) `::IMetadataRepository::UpdateLink`;
     * (3) *always* erase the L1 entry (`m_l1_->erase`) and `Delete` the
     * configured `::ICacheStore` entry best-effort, whether the
     * repository call succeeded or failed — a failed write may still have
     * committed (e.g. a post-commit timeout), so the next read must
     * reload rather than serve a value older than this write; the shared
     * cache holds non-expiring links without a TTL, so skipping it would
     * let a lost disable/delete be served indefinitely. Steps (2) and (3)
     * run under `m_write_mutex_` so this write + invalidation cannot
     * interleave with the flusher's read-modify-write of the same slug
     * (closing the in-process R-1 window; a concurrent flush can no longer
     * revert this disable/soft-delete). Concurrent updates from different
     * callers are still last-writer-wins (version is not persisted by the
     * SQL backend), and the *cross-process* flush race remains accepted
     * per decision-log D12.
     * @param link Fully-formed, already-validated updated link; must
     * already exist in the repository (matched by `link.slug`).
     * Validation stays upstream (threat model T-40).
     * @param[out] error If non-null and the call fails, receives:
     * `internal` ("invalid link record") when the codec rejects the
     * link; `not_found` ("Link not found") when the record no longer
     * exists (`RepoError::not_found`); `storage_failure` ("repository
     * failure") on any other backend error. Untouched on success.
     * @return `true` if the update was persisted; `false` on any
     * failure above.
     */
    bool update(const Link& link, AppError* error = nullptr) override;

    /**
     * @brief Records one successful redirect of @p served with no
     * repository I/O on the request thread (write-behind, plan C10).
     *
     * Request-thread steps (O(1), no I/O, never throws):
     * 1. Under `m_stats_mutex_`, accumulate into `m_pending_[served.slug]`:
     *    `redirects += 1` and `last_accessed_at = max(old, clock.now())`.
     * 2. Patch the L1 copy in place via `m_l1_->patch` (never `set`):
     *    apply the same `+1`/timestamp to the cached `Link` *without*
     *    refreshing its TTL anchor (threat model T-32), so a continuously
     *    hit but cross-process-disabled link still expires within one L1
     *    TTL. `patch` is a no-op returning `false` when the entry is
     *    absent or already expired — it never re-inserts or resurrects.
     * 3. Return `true`; never touches `m_cache_` or `m_repo_`.
     *
     * Persistence is deferred to the flusher thread (or, when
     * `stats_flush_interval == 0`, written through synchronously from the
     * caller's thread for the in-memory backend). A repository that is
     * down therefore never affects a redirect.
     * @param served The link that served the redirect.
     * @param[out] error In write-behind mode (interval > 0) left
     * untouched (deferred persistence has no synchronous failure). In
     * write-through mode (interval == 0) left untouched as well: persist
     * failures are re-queued, not surfaced, because stats are best-effort.
     * @return `true` always; stat recording never fails the caller.
     */
    bool recordRedirect(const Link& served, AppError* error = nullptr) override;

    /**
     * @brief Persists every pending redirect-stat delta now.
     *
     * Called by the flusher thread on each interval and once more on
     * shutdown; also used by tests to drive a deterministic flush instead
     * of sleeping for the timer. **Never called from a request thread.**
     *
     * Algorithm:
     * 1. Swap `m_pending_` out under `m_stats_mutex_` into a local map, so
     *    new deltas accumulate in a fresh map while this flush runs and
     *    the lock is not held across any repository call.
     * 2. For each slug, **under `m_write_mutex_`** (taken and released per
     *    slug so a long flush does not block writers): read the record
     *    fresh with `m_repo_->GetByShortCode` (bypassing L1). If it is
     *    absent with no backend error, the link was deleted upstream —
     *    drop the delta. If it is absent because of a backend error,
     *    re-queue the delta (merge back into `m_pending_`) and continue.
     *    Otherwise read the record's current stats (`get_stats_attribute`),
     *    **add** the delta to all three counters and advance
     *    `last_accessed_at`, write just that one attribute back
     *    (`set_stats_attribute`) and persist with `m_repo_->UpdateLink`.
     *    Holding `m_write_mutex_` across this read-modify-write serializes
     *    it with `create`/`update`, so an in-process `enabled`/`deleted_at`
     *    change can no longer be reverted (the former R-1 window is closed
     *    within one process); the *cross-process* flush race is unaffected
     *    and accepted per D12. On `UpdateLink` failure the delta is
     *    re-queued (no loss, no double count) and the next slug is
     *    processed.
     * 3. Every per-slug body is wrapped so a throwing repository (or
     *    `std::bad_alloc`) re-queues that slug's delta and continues
     *    instead of propagating; the flusher thread and the destructor
     *    never see an exception from here.
     * 4. Failures are coalesced: at most **one** warn line per flush cycle
     *    (`link_store_stats_flush_failed`) summarizing the failure count
     *    and the last backend error name — never a per-slug line and never
     *    any record value.
     */
    void flush_stats();

    /**
     * @brief Starts the background flusher thread.
     *
     * A no-op when `m_options_.stats_flush_interval == 0` (write-through
     * mode: `recordRedirect` persists from the caller's thread, no thread
     * is needed) or when the flusher is already running. Otherwise spawns
     * one `std::thread` that waits on a condition variable with timeout
     * `stats_flush_interval` and calls `flush_stats()` on each timeout.
     * Intended to be called once, from the owning thread, after wiring.
     */
    void start_stats_flusher();

    /**
     * @brief Stops the background flusher thread and performs a final
     * `flush_stats()`.
     *
     * Sets the stop flag, notifies the condition variable, joins the
     * thread, then runs one final, **time-boxed** flush so a delta
     * accumulated since the last tick is not lost without letting teardown
     * block unboundedly on slow backend I/O. The final flush persists
     * pending slugs until `m_options_.shutdown_flush_budget` elapses
     * (measured on a steady clock); any slugs still pending past the
     * deadline are dropped (stats are best-effort, D12) and one coalesced
     * warn (`link_store_stats_flush_dropped`) is logged. The whole final
     * flush is wrapped in a `catch (...)` so this never throws — safe to
     * call from the (implicitly `noexcept`) destructor. Idempotent and
     * safe to call when the flusher was never started (it still performs
     * the final flush). Also invoked by the destructor. Intended to run
     * from the owning (shutdown) thread only.
     */
    void stop_stats_flusher();

private:
    /**
     * @brief Accumulated, not-yet-persisted redirect counts for one slug.
     *
     * A delta is relative, never an absolute snapshot: the flusher adds it
     * to whatever the repository currently holds, so two processes against
     * one database sum their counts instead of overwriting (last-writer
     * wins only inside race R-1).
     */
    struct StatsDelta
    {
        ///< Number of redirects to add to all three monotonic counters
        ///< (`total_redirects`, `redirects_24h`, `redirects_7d`).
        std::uint64_t redirects = 0;

        ///< Most recent redirect instant seen for this slug since the
        ///< last flush; written to `last_accessed_at` on persist. Absent
        ///< until the first redirect in the current window.
        std::optional<std::chrono::system_clock::time_point> last_accessed_at;
    };

    /**
     * @brief Adds the pending delta for @p slug (if any) into @p link's
     * stats. Called on every repository load before caching/returning.
     *
     * Under `m_stats_mutex_`, looks up @p slug in `m_pending_` and, when
     * present, adds its `redirects` to all three counters and advances
     * `last_accessed_at`. This keeps "persisted counters + pending deltas"
     * as the value observed outside a flush, so an L1 TTL expiry (5 s)
     * shorter than the flush interval (30 s), or a size eviction, does not
     * regress counts. (Transient under-read race R-2 during a flush swap
     * is documented on `flush_stats`.)
     * @param slug Slug whose pending delta to apply.
     * @param[in,out] link Decoded link whose `stats` are incremented in
     * place; must not be null.
     */
    void merge_pending_stats(const std::string& slug, Link* link) const;

    /**
     * @brief Re-queues a delta that failed to persist by merging it back
     * into `m_pending_` under `m_stats_mutex_`.
     *
     * Merges rather than overwrites, because new redirects may have
     * accumulated for the slug since the flush swapped the map out, so the
     * failed delta is added to (not substituted for) the current pending
     * entry — no loss, no double count.
     * @param slug Slug the delta belongs to.
     * @param delta The delta that could not be written.
     */
    void requeue_delta(const std::string& slug, const StatsDelta& delta);

    /**
     * @brief Shared body of `flush_stats()` (no deadline) and the
     * time-boxed shutdown flush.
     *
     * Implements the algorithm documented on `flush_stats`. When @p
     * deadline is set, the per-slug loop stops once the steady clock
     * reaches it and the remaining pending deltas are dropped with one
     * coalesced `link_store_stats_flush_dropped` warn; when it is
     * `std::nullopt` every pending slug is processed (periodic/explicit
     * flush). Takes `m_write_mutex_` per slug; never propagates a
     * repository exception (each slug body is guarded, re-queuing on
     * failure).
     * @param deadline Steady-clock instant past which remaining pending
     * slugs are dropped, or `std::nullopt` to process all of them.
     */
    void drain_pending(
        std::optional<std::chrono::steady_clock::time_point> deadline);

    /**
     * @brief Flusher-thread body: waits on `m_flusher_cv_` with timeout
     * `stats_flush_interval` and calls `flush_stats()` on each timeout,
     * exiting promptly when the stop flag is set (the final flush is run
     * by `stop_stats_flusher`, not here). The `flush_stats()` call is
     * wrapped in a `catch (...)` so a throwing repository can never
     * propagate out of the thread and `std::terminate` the process.
     */
    void flusher_loop();

    ///< Authoritative metadata repository; shared, never null.
    std::shared_ptr<::IMetadataRepository> m_repo_;

    ///< Configured cache-aside store; invalidate-only, never read for
    ///< link data (T-38). May be null when no cache is configured.
    std::shared_ptr<::ICacheStore> m_cache_;

    ///< Process-local full-`Link` hot-path cache (L1); shared, never
    ///< null.
    std::shared_ptr<url_shortener::ILinkCacheStore> m_l1_;

    ///< Clock used by the store; not owned, must outlive this object.
    const IClock& m_clock_;

    ///< Scan/flush tuning.
    RepositoryLinkStoreOptions m_options_;

    ///< Coarse "store write" lock serializing `create`, `update`, and the
    ///< flusher's per-slug read-modify-write in `flush_stats`. Outer lock
    ///< in the `m_write_mutex_ -> m_stats_mutex_` order (never the
    ///< reverse). Deliberately **not** taken by `recordRedirect` or the
    ///< read paths, keeping the redirect fast path lock-free (C10).
    ///< Process-local: it does not close the cross-process flush race
    ///< (D12).
    std::mutex m_write_mutex_;

    ///< Guards `m_pending_` only; a short lock taken by `recordRedirect`,
    ///< `merge_pending_stats`, `requeue_delta`, and the swap in
    ///< `flush_stats`. Never held across repository I/O. Mutable so the
    ///< `const` read paths can merge pending deltas under it.
    mutable std::mutex m_stats_mutex_;

    ///< Slug -> not-yet-persisted redirect delta. Empty between flushes
    ///< once the flusher has drained it.
    std::unordered_map<std::string, StatsDelta> m_pending_;

    ///< Background flusher thread; joinable only while running. Not
    ///< started by the constructor (see the class lifecycle notes).
    std::thread m_flusher_thread_;

    ///< Guards `m_flusher_stop_` and backs `m_flusher_cv_`'s wait.
    std::mutex m_flusher_mutex_;

    ///< Signals the flusher to wake for a stop (or an early flush).
    std::condition_variable m_flusher_cv_;

    ///< Stop flag read by `flusher_loop`; set by `stop_stats_flusher`.
    bool m_flusher_stop_ = false;
};

}  // namespace app
}  // namespace url_shortener
