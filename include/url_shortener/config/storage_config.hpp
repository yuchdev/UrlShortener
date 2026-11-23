#pragma once
#include <optional>
#include <string>

/**
 * @brief Configuration for the metadata/link-record storage backend.
 *
 * Validated by `ParseStorageConfigYaml` / `ParseStorageConfigFile`
 * before use; `backend` must be one of `"inmemory"`, `"sqlite"`, or
 * `"postgres"`. The companion field for the chosen backend must be
 * set.
 */
struct MetadataBackendConfig
{
    ///< Backend selector; accepted values: `"inmemory"` (default),
    ///< `"sqlite"`, `"postgres"`.
    std::string backend = "inmemory";

    ///< Path to the SQLite database file; required when
    ///< `backend == "sqlite"`, ignored otherwise.
    std::optional<std::string> sqlite_path;

    ///< PostgreSQL connection string (libpq DSN or URI); required
    ///< when `backend == "postgres"`, ignored otherwise. Treat as
    ///< a secret — do not log directly; use
    ///< `observability::redactSecretValue` before logging.
    std::optional<std::string> postgres_dsn;
};

/**
 * @brief Configuration for the cache-aside storage backend.
 *
 * Validated by `ParseStorageConfigYaml` / `ParseStorageConfigFile`;
 * `backend` must be `"none"`, `"inmemory"`, or `"redis"`. The cache
 * is best-effort: failures are non-fatal to request handling.
 */
struct CacheBackendConfig
{
    ///< Backend selector; accepted values: `"none"` (default,
    ///< disables caching), `"inmemory"`, `"redis"`.
    std::string backend = "none";

    ///< Redis server address in `"host:port"` form; required when
    ///< `backend == "redis"`, ignored otherwise.
    std::optional<std::string> redis_address;

    ///< Default TTL in seconds applied when no per-entry TTL is
    ///< provided; must be >= 0 if set. Absent means no default TTL.
    std::optional<int> default_ttl_seconds;
};

/**
 * @brief Configuration for the analytics storage backend.
 *
 * Validated by `ParseStorageConfigYaml` / `ParseStorageConfigFile`;
 * `backend` must be `"noop"` or `"inmemory"`.
 */
struct AnalyticsBackendConfig
{
    ///< Backend selector; accepted values: `"noop"` (default,
    ///< discards all events), `"inmemory"`.
    std::string backend = "noop";
};

/**
 * @brief Configuration for the rate-limiting backend.
 *
 * Validated by `ParseStorageConfigYaml` / `ParseStorageConfigFile`;
 * when `enabled` is `true`, `backend` must be `"inmemory"` or
 * `"redis"`. When disabled, `BuildStorageAdapters` sets
 * `StorageAdapters::rate_limiter` to `nullptr`.
 */
struct RateLimitBackendConfig
{
    ///< When `false`, rate limiting is disabled and no limiter
    ///< adapter is constructed.
    bool enabled = false;

    ///< Backend selector (used only when `enabled`); accepted
    ///< values: `"inmemory"` (default), `"redis"`.
    std::string backend = "inmemory";

    ///< Redis server address in `"host:port"` form; required when
    ///< `enabled && backend == "redis"`, ignored otherwise.
    std::optional<std::string> redis_address;
};

/**
 * @brief Top-level storage configuration consumed by
 * `BuildStorageAdapters`.
 *
 * Groups the four independent backend sections. Obtain a populated
 * instance via `ParseStorageConfigFile` or `ParseStorageConfigYaml`;
 * do not construct by hand unless every section has been validated.
 */
struct StorageConfig
{
    ///< Configuration for the metadata (link-record) backend.
    MetadataBackendConfig metadata;

    ///< Configuration for the cache-aside backend.
    CacheBackendConfig cache;

    ///< Configuration for the analytics events backend.
    AnalyticsBackendConfig analytics;

    ///< Configuration for the rate-limiting backend.
    RateLimitBackendConfig rate_limit;
};

/**
 * @brief Parses and validates a storage configuration from a YAML
 * file on disk.
 *
 * Reads the file at `file_path`, then delegates to the same
 * validation logic as `ParseStorageConfigYaml`. All four backend
 * sections are optional in the YAML; absent sections fall back to
 * their struct defaults.
 * @param file_path Path to the YAML configuration file.
 * @return Validated `StorageConfig` populated from the file.
 * @throws std::runtime_error if any backend value is invalid or a
 * required companion field is missing (e.g. `sqlite_path` absent
 * for a `"sqlite"` metadata backend).
 * @throws yaml-cpp exceptions if the file cannot be opened or is
 * not valid YAML.
 */
StorageConfig ParseStorageConfigFile(const std::string& file_path);

/**
 * @brief Parses and validates a storage configuration from a YAML
 * string.
 *
 * All four backend sections are optional; absent sections fall back
 * to their struct defaults. Backend values are validated against the
 * accepted sets; companion fields are checked for presence when their
 * backend is selected.
 * @param yaml_content YAML text to parse.
 * @return Validated `StorageConfig` populated from `yaml_content`.
 * @throws std::runtime_error if `yaml_content` contains an invalid
 * backend value, a required companion field is absent, or
 * `cache.default_ttl_seconds` is negative.
 * @throws yaml-cpp exceptions if `yaml_content` is not valid YAML.
 */
StorageConfig ParseStorageConfigYaml(const std::string& yaml_content);
