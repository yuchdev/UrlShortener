#pragma once

#include <chrono>
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
 * `m_store_` before `m_clock_` so the store is destroyed first and the
 * clock outlives every store operation, including the destructor's
 * final flush.
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
                      std::string summary);

    friend LinkBackendHandle InitLinkBackend(const ServerConfig& config);

    ///< Built storage adapters (metadata/cache/analytics/rate_limiter);
    ///< kept alive so later tasks can reuse analytics/rate_limiter.
    ///< Empty on an inactive handle.
    StorageAdapters m_adapters_;

    ///< System clock shared (by reference) with `m_store_`; declared
    ///< after `m_store_` so it outlives every store operation.
    std::shared_ptr<IClock> m_clock_;

    ///< Installed store; null on an inactive or moved-from handle.
    ///< Destroyed before `m_clock_` (declaration order).
    std::shared_ptr<RepositoryLinkStore> m_store_;

    ///< Resolved per-backend settings.
    LinkBackendSettings m_settings_;

    ///< Redacted one-line storage summary for `summary()`.
    std::string m_summary_;
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
 * a database file created by the probe is restricted to owner
 * read/write (mode 0600 on POSIX) and the permissions of a pre-existing
 * file are never widened (carried requirement 1, threat model T-30).
 *
 * Fail-closed: any parse/validation failure or a failed probe throws
 * and nothing is installed. The thrown message never contains a DSN, a
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
 * @param config Server configuration; only `storage_config_path` is
 * read here. When it is `std::nullopt`, an inactive handle is returned
 * and nothing is installed (today's legacy in-memory default, C3/C4).
 * @return An active handle (installed store) when a path is configured
 * and the backend is reachable; an inactive handle otherwise.
 * @throws std::runtime_error with one of the fixed, value-free messages
 * above on any parse/validation/probe failure; `LinkBackend` is left
 * untouched (`installed() == false`) after a throw.
 */
LinkBackendHandle InitLinkBackend(const ServerConfig& config);

}  // namespace app
}  // namespace url_shortener
