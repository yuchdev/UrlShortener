#pragma once
#include <optional>
#include <string>

struct MetadataBackendConfig { std::string backend = "inmemory"; std::optional<std::string> sqlite_path; std::optional<std::string> postgres_dsn; };
struct CacheBackendConfig { std::string backend = "none"; std::optional<std::string> redis_address; std::optional<int> default_ttl_seconds; };
struct AnalyticsBackendConfig { std::string backend = "noop"; };
struct RateLimitBackendConfig { bool enabled = false; std::string backend = "inmemory"; std::optional<std::string> redis_address; };
struct StorageConfig { MetadataBackendConfig metadata; CacheBackendConfig cache; AnalyticsBackendConfig analytics; RateLimitBackendConfig rate_limit; };

/** Parse YAML file and validate backend-specific fields. Throws std::runtime_error on invalid configuration. */
StorageConfig ParseStorageConfigFile(const std::string& file_path);
/** Parse YAML content and validate backend-specific fields. Throws std::runtime_error on invalid configuration. */
StorageConfig ParseStorageConfigYaml(const std::string& yaml_content);
