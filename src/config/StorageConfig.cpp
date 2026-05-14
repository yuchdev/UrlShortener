#include "url_shortener/config/StorageConfig.hpp"

#include <sstream>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace {
void Require(bool ok, const std::string& msg) { if (!ok) throw std::runtime_error(msg); }
std::optional<std::string> GetString(const YAML::Node& n, const char* key) {
    if (!n[key]) return std::nullopt; return n[key].as<std::string>(); }
std::optional<int> GetInt(const YAML::Node& n, const char* key) { if (!n[key]) return std::nullopt; return n[key].as<int>(); }
}

StorageConfig ParseStorageConfigYaml(const std::string& yaml_content) {
    const YAML::Node root = YAML::Load(yaml_content);
    StorageConfig cfg;
    if (root["metadata"]) {
        const auto m = root["metadata"]; if (m["backend"]) cfg.metadata.backend = m["backend"].as<std::string>();
        cfg.metadata.sqlite_path = GetString(m, "sqlite_path"); cfg.metadata.postgres_dsn = GetString(m, "postgres_dsn");
    }
    if (root["cache"]) {
        const auto c = root["cache"]; if (c["backend"]) cfg.cache.backend = c["backend"].as<std::string>();
        cfg.cache.redis_address = GetString(c, "redis_address"); cfg.cache.default_ttl_seconds = GetInt(c, "default_ttl_seconds");
    }
    if (root["analytics"] && root["analytics"]["backend"]) cfg.analytics.backend = root["analytics"]["backend"].as<std::string>();
    if (root["rate_limit"]) {
        const auto rl = root["rate_limit"]; if (rl["enabled"]) cfg.rate_limit.enabled = rl["enabled"].as<bool>();
        if (rl["backend"]) cfg.rate_limit.backend = rl["backend"].as<std::string>(); cfg.rate_limit.redis_address = GetString(rl, "redis_address");
    }

    Require(cfg.metadata.backend == "inmemory" || cfg.metadata.backend == "sqlite" || cfg.metadata.backend == "postgres", "invalid metadata.backend: " + cfg.metadata.backend);
    if (cfg.metadata.backend == "sqlite") Require(cfg.metadata.sqlite_path && !cfg.metadata.sqlite_path->empty(), "metadata.sqlite_path is required for sqlite backend");
    if (cfg.metadata.backend == "postgres") Require(cfg.metadata.postgres_dsn && !cfg.metadata.postgres_dsn->empty(), "metadata.postgres_dsn is required for postgres backend");

    Require(cfg.cache.backend == "none" || cfg.cache.backend == "inmemory" || cfg.cache.backend == "redis", "invalid cache.backend: " + cfg.cache.backend);
    if (cfg.cache.backend == "redis") Require(cfg.cache.redis_address && !cfg.cache.redis_address->empty(), "cache.redis_address is required for redis backend");
    if (cfg.cache.default_ttl_seconds) Require(*cfg.cache.default_ttl_seconds >= 0, "cache.default_ttl_seconds must be >= 0");

    Require(cfg.analytics.backend == "noop" || cfg.analytics.backend == "inmemory", "invalid analytics.backend: " + cfg.analytics.backend);
    if (!cfg.rate_limit.enabled) return cfg;
    Require(cfg.rate_limit.backend == "inmemory" || cfg.rate_limit.backend == "redis", "invalid rate_limit.backend: " + cfg.rate_limit.backend);
    if (cfg.rate_limit.backend == "redis") Require(cfg.rate_limit.redis_address && !cfg.rate_limit.redis_address->empty(), "rate_limit.redis_address is required for redis backend");
    return cfg;
}

StorageConfig ParseStorageConfigFile(const std::string& file_path) {
    const YAML::Node root = YAML::LoadFile(file_path);
    YAML::Emitter out; out << root;
    return ParseStorageConfigYaml(out.c_str());
}
