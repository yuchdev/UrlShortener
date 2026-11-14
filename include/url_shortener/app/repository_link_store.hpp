#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>

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
 * model T-40). `recordRedirect` remains stubbed until subtask 05.
 *
 * Privacy: logs only the slug and backend error names. Decoded
 * `target_url`/tag/metadata/campaign bytes are untrusted (a foreign
 * process may have written the record) and are never logged (threat
 * model T-37; `record_to_link` warning).
 *
 * Thread-safety: safe to call concurrently. This object holds no
 * mutable state of its own; every read forwards through the
 * thread-safe collaborators (`m_repo_`, `m_l1_`, `m_cache_`) behind
 * their `shared_ptr`s. The `const` read methods mutate those
 * collaborators through the pointers, which is permitted.
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
     * best-effort. No lock is taken; slug uniqueness is enforced
     * atomically by the repository.
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
     * let a lost disable/delete be served indefinitely. No lock is taken;
     * concurrent updates are last-writer-wins (version is not
     * persisted by the SQL backend).
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
     * @brief Records a redirect against `link`. Not implemented in
     * subtask 03.
     *
     * Deliberately *not* marked `override`: `ILinkStore` does not yet
     * declare `recordRedirect` — subtask 05 adds the virtual method to
     * the interface and the write-behind body here. The stub exists so
     * the class shape matches the subtask-03 spec and callers can be
     * wired incrementally.
     * @param link Link that served a redirect (ignored by the stub).
     * @param[out] error If non-null, receives
     * `AppErrorCode::internal` with detail `"not implemented"`.
     * @return Always `false` until subtask 05 implements write-behind
     * stats.
     */
    bool recordRedirect(const Link& link, AppError* error = nullptr);

private:
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
};

}  // namespace app
}  // namespace url_shortener
