#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "url_shortener/app/repository_link_store.hpp"
#include "url_shortener/composition/storage_factory.hpp"
#include "url_shortener/config/storage_config.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/core/config.hpp"

namespace url_shortener
{
namespace app
{

/**
 * @brief Resolved tuning for the installed `RepositoryLinkStore`,
 * derived from the selected metadata backend by `InitLinkBackend`.
 *
 * The values are not read from the YAML; they are fixed per backend
 * class (in-memory versus persistent) so that the single-process
 * in-memory default keeps its current semantics while any shared,
 * persistent backend gets a bounded L1 staleness window and a
 * background write-behind flusher (carried security requirements 3 and
 * 4). Tuning knobs for these are a deliberate follow-up.
 */
struct LinkBackendSettings
{
    ///< Selected metadata backend name: `"inmemory"`, `"sqlite"`, or
    ///< `"postgres"`. Empty on an inactive handle.
    std::string metadata_backend;

    ///< Selected cache backend name: `"none"`, `"inmemory"`, or
    ///< `"redis"`. Empty on an inactive handle.
    std::string cache_backend;

    ///< TTL of the process-local L1 `ExpiringLinkCache`. `0` for the
    ///< in-memory metadata backend (the cache and the authoritative
    ///< store are the same process, so no cross-process staleness
    ///< exists and entries never expire); a strictly positive value
    ///< (5 s) for any persistent/shared backend, so a record mutated
    ///< by another process stops being served stale within one TTL.
    ///< Must be `> 0` whenever the backend is persistent (carried
    ///< requirement 3: a `0` TTL would disable expiry and reopen the
    ///< cross-process stale-redirect window, threat model T-32).
    std::chrono::seconds l1_ttl {0};

    ///< Interval of the background redirect-counter flusher. `0` for
    ///< the in-memory metadata backend (write-through from the caller
    ///< thread, no thread spawned); 30 s for any persistent backend
    ///< (write-behind: redirect requests never perform repository I/O,
    ///< shared contract C10). Write-through (`0`) is only ever paired
    ///< with the in-memory backend (carried requirement 4).
    std::chrono::seconds stats_flush_interval {0};
};

/**
 * @brief Move-only RAII handle returned by `InitLinkBackend` that owns
 * the live storage adapters and the installed `RepositoryLinkStore`.
 *
 * An *active* handle (built from a configured `storage_config_path`)
 * keeps the four `StorageAdapters`, the clock, and the installed store
 * alive for its whole lifetime, and on destruction stops the store's
 * background stats flusher — joining the thread and performing one
 * final write-behind flush while the logger singleton is still alive
 * (carried requirement 5; the store must never be destroyed during
 * static destruction). An *inactive* handle (no configured path) owns
 * nothing and installs nothing: behavior is byte-for-byte today's
 * legacy in-memory path (shared contracts C3/C4).
 *
 * The handle does **not** call `LinkBackend::reset()` on destruction:
 * it keeps the store installed until the owner (or a test) resets the
 * holder, so handlers that captured the installed store see a stable
 * pointer across the handle's teardown. `InitLinkBackend`'s caller
 * (subtask 12 `main`) is responsible for calling
 * `LinkBackend::shutdown()` before process exit to drop the holder's
 * ownership deterministically.
 *
 * Thread-safety: not thread-safe. Construct, move, `flush()`, and
 * destroy it from the owning (startup/shutdown) thread only. The
 * installed store it wires up is itself safe for concurrent request
 * threads; this handle is the startup/teardown seam, not a request
 * object.
 *
 * Lifetime: owns `m_clock_` via `shared_ptr`; `m_store_` holds a
 * non-owning `const IClock&` into it. Member declaration order places
 * `m_clock_` before `m_store_`; members are destroyed in reverse order,
 * so the store (declared last) is destroyed first and the clock outlives
 * every store operation, including the destructor's final flush. Do not
 * reorder these members.
 */
class LinkBackendHandle
{
public:
    /**
     * @brief Constructs an inactive handle that owns and installs
     * nothing.
     */
    LinkBackendHandle() = default;

    /**
     * @brief Move-constructs from @p other, transferring ownership of
     * the adapters, clock, store, and settings.
     *
     * The moved-from handle becomes inactive (its store pointer is
     * null) and performs no flush on its own destruction, so exactly
     * one final flush happens across a move (handle move-semantics
     * test).
     * @param other Source handle; left inactive.
     */
    LinkBackendHandle(LinkBackendHandle&& other) noexcept;

    /**
     * @brief Move-assigns from @p other.
     *
     * If this handle is active, its store's flusher is stopped (joining
     * + final flush) before taking over @p other's state, so no pending
     * delta is dropped and no flusher thread is leaked. The moved-from
     * handle becomes inactive.
     * @param other Source handle; left inactive.
     * @return `*this`.
     */
    LinkBackendHandle& operator=(LinkBackendHandle&& other) noexcept;

    LinkBackendHandle(const LinkBackendHandle&) = delete;
    LinkBackendHandle& operator=(const LinkBackendHandle&) = delete;

    /**
     * @brief Stops the installed store's background flusher (joins the
     * thread and performs one final flush) when active; a no-op when
     * inactive or moved-from.
     *
     * Does **not** reset `LinkBackend`; the store stays installed (see
     * the class contract). Runs the final flush while the owning thread
     * and the logger are alive, so the store is never flushed/destroyed
     * during static destruction (carried requirement 5).
     */
    ~LinkBackendHandle();

    /**
     * @brief Reports whether this handle installed a store.
     * @return `true` for a handle built from a configured
     * `storage_config_path`; `false` for an inactive or moved-from
     * handle.
     */
    bool active() const;

    /**
     * @brief Returns the authoritative record-based metadata repository the
     * installed `RepositoryLinkStore` was built over, or null in legacy mode.
     *
     * The single cross-task seam `composition::build_command_runtime`
     * (Task 03.0 subtask 18) uses to decide which browse lister to wire: a
     * non-null repository selects `app::RepositoryLinkLister` (reads the live
     * backend directly), a null one selects `app::LegacyLinkLister` over the
     * global in-memory `linkRepository()`. The returned pointer is the same
     * `StorageAdapters::metadata` adapter the installed store wraps, so a
     * lister built from it sees exactly what the store persists.
     *
     * Ownership: shares ownership via `std::shared_ptr` for the returned copy's
     * lifetime, so a lister built from it keeps the repository alive
     * independently of this handle. The handle still owns the adapters for its
     * own lifetime (see the class contract).
     *
     * @return The `::IMetadataRepository` of an active handle (never null for
     * an active handle); a null `std::shared_ptr` for an inactive or moved-from
     * handle (the legacy in-memory default, where no record-based repository
     * exists).
     */
    std::shared_ptr<::IMetadataRepository> repository() const;

    /**
     * @brief Returns the selected create-throttle rate limiter (threat model
     * T-75), used by `POST /api/v1/links` / `POST /api/v1/short-urls` to bound
     * link-creation bursts per client peer address.
     *
     * `InitLinkBackend` **always constructs** a dedicated in-memory create
     * limiter (over this handle's clock) so the throttle is on out of the box
     * with no configuration. When the storage config enables a Redis rate
     * limiter (`StorageConfig.rate_limit.enabled && backend == "redis"`) the
     * configured Redis limiter (`StorageAdapters::rate_limiter`) is returned
     * **instead**, so a multi-process deployment shares one window; any other
     * `rate_limit` state (disabled, or `backend == "inmemory"`) keeps the
     * dedicated in-memory create limiter. The selection is fixed at
     * `InitLinkBackend` time and never changes for the handle's lifetime.
     *
     * Ownership: shares ownership via `std::shared_ptr`, so a caller that keeps
     * the returned pointer keeps the limiter alive; the handle still owns the
     * limiter's clock for its own lifetime (member order keeps the clock alive
     * beyond the limiter).
     *
     * @return The selected create limiter for any handle produced by
     * `InitLinkBackend` (never null); a null `std::shared_ptr` only for a
     * default-constructed or moved-from handle (which never went through
     * `InitLinkBackend`), where callers treat a null limiter as "throttle not
     * wired" and skip it (fail-open).
     */
    std::shared_ptr<::IRateLimiter> createRateLimiter() const;

    /**
     * @brief Returns the resolved per-backend settings.
     * @return The settings for an active handle; default-constructed
     * (empty names, zero durations) for an inactive handle.
     */
    const LinkBackendSettings& settings() const;

    /**
     * @brief Returns the redacted one-line storage summary for the
     * active configuration (backend names only, never paths, DSNs, or
     * addresses). Safe to log.
     * @return `summarize_storage_config` of the active config; an empty
     * string for an inactive handle.
     */
    std::string summary() const;

    /**
     * @brief Forces an immediate write-behind stats flush now.
     *
     * Shutdown/test only; during normal operation the background
     * flusher persists the counters. A no-op when inactive or
     * moved-from. Never called from a request thread.
     */
    void flush();

private:
    /**
     * @brief Private full constructor used by `InitLinkBackend`.
     * @param adapters The built storage adapters (kept alive).
     * @param clock The system clock shared with the store.
     * @param store The installed `RepositoryLinkStore`.
     * @param settings The resolved per-backend settings.
     * @param summary The redacted one-line storage summary.
     */
    LinkBackendHandle(StorageAdapters adapters,
                      std::shared_ptr<IClock> clock,
                      std::shared_ptr<RepositoryLinkStore> store,
                      LinkBackendSettings settings,
                      std::string summary,
                      std::shared_ptr<::IRateLimiter> create_rate_limiter);

    friend LinkBackendHandle InitLinkBackend(const ServerConfig& config);

    ///< Built storage adapters (metadata/cache/analytics/rate_limiter);
    ///< kept alive so later tasks can reuse analytics/rate_limiter.
    ///< Empty on an inactive handle.
    StorageAdapters m_adapters_;

    ///< System clock shared (by reference) with `m_store_`; declared
    ///< before `m_store_` so it is destroyed after it and outlives every
    ///< store operation. Must stay above `m_store_`.
    std::shared_ptr<IClock> m_clock_;

    ///< Installed store; null on an inactive or moved-from handle.
    ///< Declared after `m_clock_`, so destroyed before it (reverse
    ///< declaration order).
    std::shared_ptr<RepositoryLinkStore> m_store_;

    ///< Resolved per-backend settings.
    LinkBackendSettings m_settings_;

    ///< Redacted one-line storage summary for `summary()`.
    std::string m_summary_;

    ///< The selected create-throttle limiter (threat model T-75) returned by
    ///< @ref createRateLimiter. Either the dedicated in-memory create limiter
    ///< (default), which borrows `m_clock_` by reference, or the configured
    ///< Redis limiter aliased from `m_adapters_.rate_limiter`. Declared after
    ///< `m_clock_` (and the adapters) so it is destroyed before them: an
    ///< in-memory create limiter must never outlive the clock it borrows.
    ///< Null only on a default-constructed/moved-from handle.
    std::shared_ptr<::IRateLimiter> m_create_rate_limiter_;
};

/**
 * @brief Builds the single redacted storage summary string.
 *
 * Emits `"metadata=<b> cache=<b> analytics=<b> rate_limit=<b|off>"`
 * using backend names only. A pure function with no I/O; it never
 * includes `sqlite_path`, a `postgres_dsn`, a `redis_address`, a host,
 * or any secret, so the result is always safe to log (CLAUDE.md
 * secret-handling rules). `rate_limit` is the literal `off` when rate
 * limiting is disabled.
 * @param config Validated storage configuration.
 * @return The redacted summary string.
 */
std::string summarize_storage_config(const StorageConfig& config);

/**
 * @brief Turns `ServerConfig::storage_config_path` into a live,
 * installed `RepositoryLinkStore` and returns the owning handle.
 *
 * Sequence when a path is configured: parse and validate the YAML ->
 * `BuildStorageAdapters` -> probe the metadata backend (forcing
 * create/open and schema bootstrap, so a bad path or unreachable
 * database fails at startup) -> construct the L1 cache and store ->
 * `LinkBackend::install` -> start the background flusher (persistent
 * backends only) -> return the handle. On a persistent SQLite backend,
 * the probe runs under a restrictive process umask (0077 on POSIX) so the
 * database file *and* its `-journal`/`-wal`/`-shm` companions are created
 * owner-only (mode 0600) with no world/group-readable TOCTOU window; the
 * file is also chmod-ed to 0600 as defense in depth, and the permissions
 * of a pre-existing file are never widened (carried requirement 1, threat
 * model T-30). The umask change is process-wide and restored by an RAII
 * guard (also on exceptions); it is safe because this runs single-threaded
 * at startup, before any request/flusher thread exists.
 *
 * Fail-closed: any parse/validation failure, a failed probe, or a failure
 * starting the background flusher throws and leaves nothing installed —
 * on a flusher-start failure `InitLinkBackend` runs `LinkBackend::shutdown`
 * before rethrowing so the store is never stranded globally installed
 * with no handle to stop it. The thrown message never contains a DSN, a
 * password, a path, or file contents (carried requirement 2, threat
 * model T-31):
 * - `"storage config: cannot read file"` when the YAML file cannot be
 *   opened (`YAML::BadFile`);
 * - `"storage config: invalid configuration"` for any other yaml-cpp
 *   parse/conversion error (its `what()` may echo an adjacent
 *   `postgres_dsn`/`redis_address` scalar and is therefore never
 *   surfaced);
 * - `"storage config: <field/backend reason>"` for a validation failure
 *   from `ParseStorageConfig*` (field/backend names only, no values);
 * - `"storage backend unavailable"` when the metadata probe reports a
 *   backend error.
 *
 * Side effect: publishes the process-wide `system.info` snapshot via
 * `app::set_process_system_info` exactly once, after the config is validated
 * and before any command runtime or transport can read it — from the validated
 * `StorageConfig` when a path is configured, or from a default `StorageConfig`
 * (the inmemory/none/noop/disabled backends the legacy path actually uses)
 * paired with @p config otherwise, so the snapshot always reflects the live
 * shortener settings. The snapshot carries only allowlisted, non-secret values
 * and is never logged (threat model T-53, milestone contract C6).
 *
 * @param config Server configuration; `storage_config_path` selects the
 * backend, and the non-secret shortener/request-hardening fields feed the
 * published `system.info` snapshot. When `storage_config_path` is
 * `std::nullopt`, an inactive handle is returned and nothing is installed
 * (today's legacy in-memory default, C3/C4).
 * @return An active handle (installed store) when a path is configured
 * and the backend is reachable; an inactive handle otherwise.
 * @throws std::runtime_error with one of the fixed, value-free messages
 * above on any parse/validation/probe failure. A failure starting the
 * background flusher propagates the originating exception (e.g. the
 * `std::system_error` from `std::thread`) instead. In every case
 * `LinkBackend` is left uninstalled (`installed() == false`) after the
 * throw.
 */
LinkBackendHandle InitLinkBackend(const ServerConfig& config);

namespace detail
{

/**
 * @brief Test-only seam replacing the post-`install()` flusher-start step
 * inside `InitLinkBackend`, so a test can force that step to throw and
 * verify the fail-closed rollback (nothing stays installed after a throw).
 *
 * When a non-null hook is set, `InitLinkBackend` calls it *instead of*
 * `RepositoryLinkStore::start_stats_flusher()` immediately after
 * `LinkBackend::install()`. If the hook throws, `InitLinkBackend` runs
 * `LinkBackend::shutdown()` and rethrows, exactly as it would for a real
 * `start_stats_flusher()` failure (e.g. `std::thread` creation failure).
 * The hook is null in production (the real flusher start runs for
 * persistent backends). The seam lives here rather than in
 * `RepositoryLinkStore` so no test-only branch enters the store hot path.
 *
 * Not thread-safe and process-wide: set and clear it only from a
 * single-threaded test around one `InitLinkBackend` call, and always clear
 * it afterwards (pass `nullptr`) so it never leaks into another test.
 * @param hook Replacement start step, or `nullptr` to restore the default
 * (real flusher start).
 */
void set_start_flusher_hook_for_testing(std::function<void()> hook);

/**
 * @brief Pure selection rule behind `LinkBackendHandle::createRateLimiter`
 * (threat model T-75), exposed so the Redis-vs-in-memory choice is unit-testable
 * with stub limiters without standing up a real Redis backend.
 *
 * Returns @p configured_rate_limiter when the storage config selects a Redis
 * rate limiter (`config.rate_limit.enabled && config.rate_limit.backend ==
 * "redis"`) **and** that limiter is non-null; otherwise returns
 * @p default_in_memory (the dedicated in-memory create limiter). Any other
 * `rate_limit` state (disabled, or `backend == "inmemory"`) keeps the in-memory
 * default. No I/O; a deterministic pure function.
 *
 * @param config The validated storage configuration whose `rate_limit` section
 * drives the choice.
 * @param configured_rate_limiter The `StorageAdapters::rate_limiter` built from
 * the config (the Redis limiter when `rate_limit` is enabled with that backend;
 * may be null when `rate_limit` is disabled).
 * @param default_in_memory The dedicated in-memory create limiter constructed
 * unconditionally by `InitLinkBackend`.
 * @return The limiter to expose via `createRateLimiter`.
 */
std::shared_ptr<::IRateLimiter> select_create_rate_limiter(
    const StorageConfig& config,
    std::shared_ptr<::IRateLimiter> configured_rate_limiter,
    std::shared_ptr<::IRateLimiter> default_in_memory);

}  // namespace detail

}  // namespace app
}  // namespace url_shortener
