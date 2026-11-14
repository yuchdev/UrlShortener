#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "url_shortener/composition/link_backend_bootstrap.hpp"

#include <yaml-cpp/yaml.h>

#include "url_shortener/app/expiring_link_cache.hpp"
#include "url_shortener/app/link_backend.hpp"
#include "url_shortener/storage/errors/repo_error.hpp"

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
 * @brief Restricts a freshly created SQLite database file to owner
 * read/write (mode 0600) without ever widening an existing file.
 *
 * Called only for the `sqlite` backend and only when @p pre_existed is
 * `false` (the probe created the file this run), so operator-chosen
 * permissions on a pre-existing database are never relaxed (carried
 * requirement 1, threat model T-30). A no-op for `":memory:"` and on
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

LinkBackendHandle::LinkBackendHandle(StorageAdapters adapters,
                                     std::shared_ptr<IClock> clock,
                                     std::shared_ptr<RepositoryLinkStore> store,
                                     LinkBackendSettings settings,
                                     std::string summary)
    : m_adapters_(std::move(adapters))
    , m_clock_(std::move(clock))
    , m_store_(std::move(store))
    , m_settings_(std::move(settings))
    , m_summary_(std::move(summary))
{
}

LinkBackendHandle::LinkBackendHandle(LinkBackendHandle&& other) noexcept
    : m_adapters_(std::move(other.m_adapters_))
    , m_clock_(std::move(other.m_clock_))
    , m_store_(std::move(other.m_store_))
    , m_settings_(std::move(other.m_settings_))
    , m_summary_(std::move(other.m_summary_))
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
    //    (today's legacy in-memory default, C3/C4).
    if (!config.storage_config_path) {
        return LinkBackendHandle {};
    }

    // 2. Parse + validate; all failures map to fixed, secret-free
    //    messages (T-31).
    const StorageConfig cfg =
        parse_config_or_throw(*config.storage_config_path);

    const bool persistent = cfg.metadata.backend != "inmemory";

    // 3. Build adapters over a system clock shared with the store.
    auto clock = std::make_shared<SystemClock>();
    StorageAdapters adapters = BuildStorageAdapters(cfg, *clock);

    // For the 0600 requirement: record whether a SQLite file already
    // exists *before* the probe creates it (T-30).
    bool sqlite_pre_existed = true;
    if (cfg.metadata.backend == "sqlite" && cfg.metadata.sqlite_path) {
        std::error_code ec;
        sqlite_pre_existed =
            std::filesystem::exists(*cfg.metadata.sqlite_path, ec) && !ec;
    }

    // 4. Probe: forces create/open + schema bootstrap and surfaces a
    //    bad path or unreachable backend as a fail-closed startup error.
    ::RepoError probe_error = ::RepoError::none;
    adapters.metadata->Exists(k_probe_slug, &probe_error);
    if (probe_error != ::RepoError::none
        && probe_error != ::RepoError::not_found) {
        throw std::runtime_error("storage backend unavailable");
    }

    // 1 (T-30): tighten a freshly created SQLite file to 0600; never
    // widen an existing one.
    if (cfg.metadata.backend == "sqlite" && cfg.metadata.sqlite_path) {
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

    LinkBackend::install(store);

    // Requirement 4: start the background flusher after install, for
    // persistent backends only (write-through in-memory starts nothing).
    if (settings.stats_flush_interval > std::chrono::seconds(0)) {
        store->start_stats_flusher();
    }

    return LinkBackendHandle {std::move(adapters),
                              std::move(clock),
                              std::move(store),
                              std::move(settings),
                              summarize_storage_config(cfg)};
}

}  // namespace app
}  // namespace url_shortener
