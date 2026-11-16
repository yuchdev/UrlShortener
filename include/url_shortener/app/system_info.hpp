#pragma once

#include <cstdint>
#include <optional>
#include <string>

// Forward declarations: `make_system_info` reads these two config aggregates by
// const reference and copies out only the allowlisted, non-secret fields below,
// so this header needs no definition of either type (and must not pull in
// `core/config.hpp`, which transitively includes Beast). Both live in the
// global namespace. Their full definitions are included by the `.cpp` that
// implements `make_system_info`.
struct StorageConfig;
struct ServerConfig;

namespace url_shortener
{
namespace app
{

/**
 * @brief The non-secret subset of `ServerConfig` surfaced by `system.info`
 * (threat model T-53).
 *
 * Every member is an explicitly-named, non-secret shortener/request-hardening
 * field copied verbatim from `ServerConfig` by @ref make_system_info. The
 * struct must never gain a `tls`, `analytics_client_hash_salt`, or network
 * (`http_*`/`hsts_*`) field: adding any `ServerConfig` field here is a
 * conscious allowlist decision that is re-reviewed against the secret test
 * (milestone contract C6).
 */
struct ShortenerSettings
{
    ///< `ServerConfig::shortener_base_domain`, verbatim. The configured base
    ///< URL (e.g. `http://localhost:8000`); not a secret.
    std::string base_domain;

    ///< `ServerConfig::shortener_generated_slug_length`: the length of a
    ///< server-generated slug.
    std::uint32_t generated_slug_length = 0;

    ///< `ServerConfig::shortener_default_redirect_type`: the default redirect
    ///< type token (e.g. `"temporary"`).
    std::string default_redirect_type;

    ///< `ServerConfig::shortener_default_expiry_seconds`: the default link
    ///< lifetime in seconds, or `std::nullopt` when unset (serialized `null`).
    std::optional<std::uint32_t> default_expiry_seconds;

    ///< `ServerConfig::shortener_allow_private_targets`: whether links may
    ///< point at private/loopback hosts (SSRF posture).
    bool allow_private_targets = false;

    ///< `ServerConfig::analytics_enabled`: whether the redirect analytics
    ///< pipeline is on.
    bool analytics_enabled = false;

    ///< `ServerConfig::max_request_body_bytes`: the request-body size cap.
    std::uint32_t max_request_body_bytes = 0;

    ///< `ServerConfig::max_request_target_length`: the request-target length
    ///< cap.
    std::uint32_t max_request_target_length = 0;

    ///< `ServerConfig::request_id_max_length`: the accepted request-id length
    ///< cap.
    std::uint32_t request_id_max_length = 0;
};

/**
 * @brief A non-secret snapshot of the running server's version, selected
 * storage backends and shortener settings, backing `system.info`.
 *
 * Every backend field holds a value drawn from a fixed per-section allowlist
 * (any other configured value is reported as `"unknown"`), never the raw
 * config string, so a DSN/path/address mis-set in a `backend` field cannot
 * leak (threat model T-53). The struct carries no DSN, file path, address,
 * key, password or salt.
 */
struct SystemInfo
{
    ///< The build version: `URL_SHORTENER_VERSION` when the compile definition
    ///< is present, otherwise `"0.0.0-dev"`.
    std::string version;

    ///< Metadata backend name, one of `"inmemory"`, `"sqlite"`, `"postgres"`,
    ///< or `"unknown"` for any other configured value.
    std::string metadata;

    ///< Cache backend name, one of `"none"`, `"inmemory"`, `"redis"`, or
    ///< `"unknown"` for any other configured value.
    std::string cache;

    ///< Analytics backend name, one of `"noop"`, `"inmemory"`, `"sqlite"`,
    ///< `"postgres"`, or `"unknown"` for any other configured value.
    std::string analytics;

    ///< Rate-limit backend name: `"disabled"` whenever rate limiting is off
    ///< (regardless of the configured backend), otherwise one of `"inmemory"`,
    ///< `"redis"`, or `"unknown"`.
    std::string rate_limit;

    ///< `true` when the metadata backend persists data across restarts, i.e.
    ///< the normalized metadata name is `"sqlite"` or `"postgres"`.
    bool persistent = false;

    ///< The non-secret shortener/request-hardening settings subset.
    ShortenerSettings shortener;
};

/**
 * @brief Builds a @ref SystemInfo from the two config aggregates, reading only
 * allowlisted backend names and the explicitly-named non-secret `ServerConfig`
 * fields.
 *
 * Reads `StorageConfig::metadata.backend`, `cache.backend`,
 * `analytics.backend`, `rate_limit.enabled`/`rate_limit.backend`, normalizing
 * each backend string
 * against its fixed allowlist (any value outside the allowlist becomes
 * `"unknown"`). From @p server_config it copies only the fields listed in
 * @ref ShortenerSettings. It never reads `StorageConfig::metadata.sqlite_path`,
 * `metadata.postgres_dsn`, `cache.redis_address`, `rate_limit.redis_address`,
 * `ServerConfig::tls`, or `ServerConfig::analytics_client_hash_salt` (threat
 * model T-53, milestone contract C6).
 *
 * @param storage_config The storage configuration whose four backend-name
 * fields and `rate_limit.enabled` flag are read; no secret-bearing field is
 * touched.
 * @param server_config The server configuration whose non-secret shortener and
 * request-hardening fields are copied; `tls` and `analytics_client_hash_salt`
 * are never read.
 * @return A fully populated snapshot carrying only non-secret values.
 */
SystemInfo make_system_info(const ::StorageConfig& storage_config,
                            const ::ServerConfig& server_config);

/**
 * @brief Installs the process-wide @ref SystemInfo snapshot read by
 * `build_command_runtime` (subtask 18).
 *
 * The startup bootstrap that parses `--storage-config` (Task 02.0) calls this
 * once, after which @ref process_system_info returns @p info. Before the first
 * call the process snapshot is
 * `make_system_info(StorageConfig{}, ServerConfig{})` (the in-memory
 * defaults). Thread-safe: concurrent @ref process_system_info
 * reads observe either the previous or the new value atomically, never a torn
 * one.
 *
 * @param info The snapshot to publish process-wide; taken by value and stored.
 */
void set_process_system_info(SystemInfo info);

/**
 * @brief Returns a copy of the process-wide @ref SystemInfo snapshot.
 *
 * Returns `make_system_info(StorageConfig{}, ServerConfig{})` until
 * @ref set_process_system_info has been called, and the most recently set value
 * thereafter. Thread-safe against concurrent readers and a concurrent writer.
 *
 * @return A copy of the current process-wide snapshot.
 */
SystemInfo process_system_info();

/**
 * @brief Serializes a @ref SystemInfo plus the caller's read-only flag to the
 * `system.info` result JSON.
 *
 * Emits the fixed key order
 * `{"version","backend":{"metadata","cache","analytics","rate_limit"},
 * "persistent","read_only","shortener":{...}}`. Every string value is escaped
 * with `url_shortener::jsonString`; `shortener.default_expiry_seconds` is
 * emitted as `null` when unset and as the number otherwise. No value outside
 * the allowlisted names, the non-secret `ShortenerSettings` fields, the
 * booleans,
 * and the version string is ever written (milestone contract C6).
 *
 * @param info The snapshot to serialize.
 * @param read_only The caller-derived read-only flag (`true` when the caller's
 * `ControlSet` cannot write), written as the top-level `read_only` field.
 * @return The complete `system.info` result JSON object as text.
 */
std::string serializeSystemInfoJson(const SystemInfo& info, bool read_only);

}  // namespace app
}  // namespace url_shortener
