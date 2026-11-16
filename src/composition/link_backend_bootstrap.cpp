#include <cassert>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

#include "url_shortener/composition/link_backend_bootstrap.hpp"

#include <yaml-cpp/yaml.h>

#ifndef _WIN32
#    include <sys/stat.h>  // umask
#endif

#include "url_shortener/app/expiring_link_cache.hpp"
#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/app/system_info.hpp"
#include "url_shortener/storage/errors/repo_error.hpp"
#include "url_shortener/storage/inmemory/in_memory_rate_limiter.hpp"

namespace url_shortener
{
namespace app
{

namespace
{

/// L1 cache TTL for a shared/persistent metadata backend. Strictly
/// positive so a record mutated by another process stops being served
/// stale within one TTL (threat model T-32); never 0 for a persistent
/// backend (carried requirement 3).
constexpr std::chrono::seconds k_persistent_l1_ttl {5};

/// Background write-behind flush interval for a persistent metadata
/// backend. Non-zero so redirect requests never persist counters
/// themselves (shared contract C10); never 0 (write-through) for a
/// persistent backend (carried requirement 4).
constexpr std::chrono::seconds k_persistent_stats_flush_interval {30};

/// Slug used to probe the metadata backend at startup. Never created;
/// `Exists` forces the backend open and the schema bootstrapped.
constexpr const char* k_probe_slug = "health-probe-slug";

/**
 * @brief RAII guard that tightens the process umask to `0077` for its
 * lifetime and restores the previous value on destruction, including
 * during stack unwinding on an exception.
 *
 * Used around the startup probe so that every file SQLite creates on
 * first open — the main `.db` **and** its `-journal`/`-wal`/`-shm`
 * companions, which a post-creation `chmod` of the main file alone would
 * leave world/group-readable between creation and the `chmod` (TOCTOU,
 * threat model T-30) — is born owner-only (mode `0600`). The companions
 * carry page data; under a default `0022` umask SQLite would otherwise
 * create them at `0644`.
 *
 * `umask(2)` is **process-wide** and not thread-safe: this guard is only
 * ever constructed at single-threaded startup, before any request or
 * stats-flusher thread exists (`LinkBackend::serving()` is `false`); the
 * caller asserts that precondition. A no-op on Windows, which has no
 * POSIX umask (the file inherits the account ACL instead).
 */
class ScopedRestrictiveUmask
{
public:
    /// Sets the process umask to `0077`, remembering the prior value.
    ScopedRestrictiveUmask()
#ifndef _WIN32
        : m_previous_umask_(::umask(0077))
#endif
    {
    }

    /// Restores the umask captured at construction.
    ~ScopedRestrictiveUmask()
    {
#ifndef _WIN32
        ::umask(m_previous_umask_);
#endif
    }

    ScopedRestrictiveUmask(const ScopedRestrictiveUmask&) = delete;
    ScopedRestrictiveUmask& operator=(const ScopedRestrictiveUmask&) = delete;
    ScopedRestrictiveUmask(ScopedRestrictiveUmask&&) = delete;
    ScopedRestrictiveUmask& operator=(ScopedRestrictiveUmask&&) = delete;

#ifndef _WIN32
private:
    ///< Umask value in effect before this guard replaced it with `0077`.
    ::mode_t m_previous_umask_;
#endif
};

/**
 * @brief Process-wide test seam that, when set, replaces the
 * post-`install()` flusher-start step inside `InitLinkBackend`.
 *
 * Null in production so the real `RepositoryLinkStore::start_stats_flusher`
 * runs. A test installs a throwing hook to exercise the fail-closed
 * rollback path deterministically (see
 * `detail::set_start_flusher_hook_for_testing`). Not thread-safe; only
 * ever set/cleared from a single-threaded test around one `InitLinkBackend`
 * call.
 * @return Reference to the single storage slot for the hook.
 */
std::function<void()>& start_flusher_hook()
{
    static std::function<void()> hook;
    return hook;
}

/**
 * @brief Restricts a freshly created SQLite database file to owner
 * read/write (mode 0600) without ever widening an existing file.
 *
 * Defense in depth behind the startup `ScopedRestrictiveUmask`, which
 * already forces the main file and its journal companions to be born
 * `0600`. Called only for the `sqlite` backend and only when
 * @p pre_existed is `false` (the probe created the file this run), so
 * operator-chosen permissions on a pre-existing database are never relaxed
 * (carried requirement 1, threat model T-30). A no-op for `":memory:"`,
 * when @p path is a symlink (the symlink target is operator-owned and the
 * `pre_existed` check already protects symlink->existing-file), and on
 * non-POSIX platforms (Windows has no equivalent POSIX mode; the file
 * inherits the account ACL).
 * @param path SQLite database path from the validated config.
 * @param pre_existed Whether the file existed before the probe.
 */
void restrict_new_sqlite_file(const std::string& path, bool pre_existed)
{
#ifndef _WIN32
    if (pre_existed || path == ":memory:") {
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return;
    }
    // std::filesystem::permissions follows symlinks; skip the chmod when
    // the configured path is itself a symlink so this never rewrites the
    // mode of an operator-owned file the symlink points at.
    if (std::filesystem::is_symlink(path, ec) && !ec) {
        return;
    }
    // owner_read | owner_write, replacing any group/world bits the
    // process umask would otherwise have left.
    std::filesystem::permissions(path,
                                 std::filesystem::perms::owner_read
                                     | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 ec);
    // Best-effort: a permissions failure is not fatal to startup. The
    // 0600 recommendation is also documented for operators (T-30).
    (void)ec;
#else
    (void)path;
    (void)pre_existed;
#endif
}

/**
 * @brief Parses the storage config file, mapping every failure to a
 * fixed, value-free `std::runtime_error` (carried requirement 2).
 *
 * `YAML::BadFile` (file-not-found/unreadable) and any other yaml-cpp
 * parse/conversion error are mapped to fixed phrases that cannot echo a
 * secret; `ParseStorageConfig*`'s own validation `std::runtime_error`
 * carries field/backend names only and is forwarded with a prefix.
 * @param path Storage config file path.
 * @return Validated `StorageConfig`.
 * @throws std::runtime_error with a fixed, secret-free message.
 */
StorageConfig parse_config_or_throw(const std::string& path)
{
    try {
        return ParseStorageConfigFile(path);
    } catch (const YAML::BadFile&) {
        throw std::runtime_error("storage config: cannot read file");
    } catch (const YAML::Exception&) {
        // yaml-cpp parser/BadConversion messages can include the
        // offending scalar (e.g. a line adjacent to postgres_dsn /
        // redis_address), so the raw what() is never surfaced.
        throw std::runtime_error("storage config: invalid configuration");
    } catch (const std::runtime_error& e) {
        // Require()/validation failures: field and backend names only,
        // no values (verified in storage_config.cpp).
        throw std::runtime_error(std::string("storage config: ") + e.what());
    }
}

}  // namespace

std::string summarize_storage_config(const StorageConfig& config)
{
    const std::string rate_limit = config.rate_limit.enabled
        ? config.rate_limit.backend
        : std::string("off");
    return "metadata=" + config.metadata.backend + " cache="
        + config.cache.backend + " analytics=" + config.analytics.backend
        + " rate_limit=" + rate_limit;
}

LinkBackendHandle::LinkBackendHandle(
    StorageAdapters adapters,
    std::shared_ptr<IClock> clock,
    std::shared_ptr<RepositoryLinkStore> store,
    LinkBackendSettings settings,
    std::string summary,
    std::shared_ptr<::IRateLimiter> create_rate_limiter)
    : m_adapters_(std::move(adapters))
    , m_clock_(std::move(clock))
    , m_store_(std::move(store))
    , m_settings_(std::move(settings))
    , m_summary_(std::move(summary))
    , m_create_rate_limiter_(std::move(create_rate_limiter))
{
}

LinkBackendHandle::LinkBackendHandle(LinkBackendHandle&& other) noexcept
    : m_adapters_(std::move(other.m_adapters_))
    , m_clock_(std::move(other.m_clock_))
    , m_store_(std::move(other.m_store_))
    , m_settings_(std::move(other.m_settings_))
    , m_summary_(std::move(other.m_summary_))
    , m_create_rate_limiter_(std::move(other.m_create_rate_limiter_))
{
    other.m_store_.reset();
}

LinkBackendHandle& LinkBackendHandle::operator=(
    LinkBackendHandle&& other) noexcept
{
    if (this == &other) {
        return *this;
    }
    // Flush + stop our own store before dropping it so no delta is lost
    // and no flusher thread is leaked.
    if (m_store_) {
        m_store_->stop_stats_flusher();
    }
    m_adapters_ = std::move(other.m_adapters_);
    m_clock_ = std::move(other.m_clock_);
    m_store_ = std::move(other.m_store_);
    m_settings_ = std::move(other.m_settings_);
    m_summary_ = std::move(other.m_summary_);
    m_create_rate_limiter_ = std::move(other.m_create_rate_limiter_);
    other.m_store_.reset();
    return *this;
}

LinkBackendHandle::~LinkBackendHandle()
{
    if (m_store_) {
        m_store_->stop_stats_flusher();
    }
}

bool LinkBackendHandle::active() const
{
    return m_store_ != nullptr;
}

std::shared_ptr<::IMetadataRepository> LinkBackendHandle::repository() const
{
    // The installed RepositoryLinkStore was built over m_adapters_.metadata;
    // an inactive/moved-from handle holds a default-constructed StorageAdapters
    // whose metadata is null (the legacy in-memory default has no record-based
    // repository).
    return m_adapters_.metadata;
}

std::shared_ptr<::IRateLimiter> LinkBackendHandle::createRateLimiter() const
{
    return m_create_rate_limiter_;
}

const LinkBackendSettings& LinkBackendHandle::settings() const
{
    return m_settings_;
}

std::string LinkBackendHandle::summary() const
{
    return m_summary_;
}

void LinkBackendHandle::flush()
{
    if (m_store_) {
        m_store_->flush_stats();
    }
}

LinkBackendHandle InitLinkBackend(const ServerConfig& config)
{
    // 1. No path configured -> inactive handle, nothing installed
    //    (today's legacy in-memory default, C3/C4). Publish the process-wide
    //    system.info snapshot from a *default* StorageConfig (the backends this
    //    mode actually uses: inmemory/none/noop/disabled) paired with the real
    //    ServerConfig, so system.info reflects the live shortener settings even
    //    on the legacy path. Set exactly once, before any command runtime or
    //    transport can read process_system_info(). Never logs the snapshot or
    //    any config value (threat model T-53, milestone contract C6).
    if (!config.storage_config_path) {
        set_process_system_info(make_system_info(StorageConfig {}, config));
        // The create throttle is on by default even on the legacy in-memory
        // path (threat model T-75): construct a dedicated in-memory create
        // limiter over a system clock the handle owns. The handle stays
        // inactive (no store/adapters) so redirect/CRUD behavior is byte-for-
        // byte today's default (C3/C4); only createRateLimiter() is populated.
        LinkBackendHandle handle;
        handle.m_clock_ = std::make_shared<SystemClock>();
        handle.m_create_rate_limiter_ =
            std::make_shared<InMemoryRateLimiter>(*handle.m_clock_);
        return handle;
    }

    // 2. Parse + validate; all failures map to fixed, secret-free
    //    messages (T-31).
    const StorageConfig cfg =
        parse_config_or_throw(*config.storage_config_path);

    // Publish the process-wide system.info snapshot from the validated config,
    // once, after validation and before any command runtime/transport is built
    // (the probe and store construction below never read it). make_system_info
    // copies only allowlisted backend names and the non-secret ServerConfig
    // subset, never a DSN/path/salt/TLS; the snapshot is never logged (T-53/C6).
    set_process_system_info(make_system_info(cfg, config));

    const bool persistent = cfg.metadata.backend != "inmemory";

    // 3. Build adapters over a system clock shared with the store.
    auto clock = std::make_shared<SystemClock>();
    StorageAdapters adapters = BuildStorageAdapters(cfg, *clock);

    // For the 0600 requirement: record whether a SQLite file already
    // exists *before* the probe creates it (T-30).
    const bool sqlite_file_backend = cfg.metadata.backend == "sqlite"
        && cfg.metadata.sqlite_path && *cfg.metadata.sqlite_path != ":memory:";
    bool sqlite_pre_existed = true;
    if (sqlite_file_backend) {
        std::error_code ec;
        sqlite_pre_existed =
            std::filesystem::exists(*cfg.metadata.sqlite_path, ec) && !ec;
    }

    // 4. Probe: forces create/open + schema bootstrap and surfaces a
    //    bad path or unreachable backend as a fail-closed startup error.
    //    For a file-backed SQLite backend the probe (and the schema
    //    bootstrap write it triggers) is the moment the DB file and its
    //    -journal/-wal/-shm companions are first created, so run it under
    //    a restrictive umask (0077) so every one of those files is born
    //    owner-only (0600) instead of inheriting a 0644 default — closing
    //    the TOCTOU/companions gap a post-creation chmod of the main file
    //    alone would leave (T-30). umask is process-wide; this runs at
    //    single-threaded startup, before any request/flusher thread.
    assert(!LinkBackend::serving());
    ::RepoError probe_error = ::RepoError::none;
    if (sqlite_file_backend) {
        const ScopedRestrictiveUmask umask_guard;
        (void)umask_guard;
        adapters.metadata->Exists(k_probe_slug, &probe_error);
    }
    else {
        adapters.metadata->Exists(k_probe_slug, &probe_error);
    }
    if (probe_error != ::RepoError::none
        && probe_error != ::RepoError::not_found)
    {
        throw std::runtime_error("storage backend unavailable");
    }

    // 1 (T-30): tighten a freshly created SQLite file to 0600; never
    // widen an existing one. Defense in depth behind the umask guard above.
    if (sqlite_file_backend) {
        restrict_new_sqlite_file(*cfg.metadata.sqlite_path, sqlite_pre_existed);
    }

    // 5. Resolve per-backend settings. Persistent backends get a bounded
    //    L1 TTL (> 0, requirement 3) and a background flusher interval
    //    (> 0 / write-behind, requirement 4); in-memory keeps 0/0.
    LinkBackendSettings settings;
    settings.metadata_backend = cfg.metadata.backend;
    settings.cache_backend = cfg.cache.backend;
    if (persistent) {
        settings.l1_ttl = k_persistent_l1_ttl;
        settings.stats_flush_interval = k_persistent_stats_flush_interval;
    }

    // 6. Construct the L1 cache and the store, then install and (for a
    //    persistent backend) start the flusher.
    auto l1 = std::make_shared<ExpiringLinkCache>(*clock, settings.l1_ttl);
    RepositoryLinkStoreOptions options;
    options.stats_flush_interval = settings.stats_flush_interval;
    auto store = std::make_shared<RepositoryLinkStore>(
        adapters.metadata, adapters.cache, l1, *clock, options);

    // Fail closed: install() publishes the store process-wide, but the
    // flusher start that follows can throw (e.g. std::thread creation
    // failure). If it does, a bare install() would strand the store
    // globally installed with no handle to stop it, to be destroyed during
    // static destruction (when the logger it flushes through may be gone,
    // carried requirement 5). Wrapping both steps so a throw triggers
    // LinkBackend::shutdown() guarantees nothing stays installed after a
    // throw. The start_flusher_hook() seam lets a test force the throw
    // (detail::set_start_flusher_hook_for_testing); it is null in
    // production, where the real flusher starts for persistent backends
    // only (requirement 4; write-through in-memory starts nothing).
    try {
        LinkBackend::install(store);
        if (start_flusher_hook()) {
            start_flusher_hook()();
        }
        else if (settings.stats_flush_interval > std::chrono::seconds(0)) {
            store->start_stats_flusher();
        }
    } catch (...) {
        LinkBackend::shutdown();
        throw;
    }

    // Create throttle (threat model T-75): always build a dedicated in-memory
    // create limiter over the store's clock, then select the limiter
    // createRateLimiter() exposes — the configured Redis limiter when the
    // config selects it (shared window across processes), else the in-memory
    // default. The limiter borrows *clock by reference; clock is moved into the
    // handle below (shared_ptr move keeps the pointed-to clock alive).
    auto in_memory_create_limiter =
        std::make_shared<InMemoryRateLimiter>(*clock);
    std::shared_ptr<::IRateLimiter> create_rate_limiter =
        detail::select_create_rate_limiter(
            cfg, adapters.rate_limiter, std::move(in_memory_create_limiter));

    return LinkBackendHandle {std::move(adapters),
                              std::move(clock),
                              std::move(store),
                              std::move(settings),
                              summarize_storage_config(cfg),
                              std::move(create_rate_limiter)};
}

namespace detail
{

void set_start_flusher_hook_for_testing(std::function<void()> hook)
{
    start_flusher_hook() = std::move(hook);
}

std::shared_ptr<::IRateLimiter> select_create_rate_limiter(
    const StorageConfig& config,
    std::shared_ptr<::IRateLimiter> configured_rate_limiter,
    std::shared_ptr<::IRateLimiter> default_in_memory)
{
    if (config.rate_limit.enabled && config.rate_limit.backend == "redis"
        && configured_rate_limiter)
    {
        return configured_rate_limiter;
    }
    return default_in_memory;
}

}  // namespace detail

}  // namespace app
}  // namespace url_shortener
