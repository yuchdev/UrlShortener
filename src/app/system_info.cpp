/**
 * @file system_info.cpp
 * @brief Allowlist normalization of the selected storage backends, the
 * process-wide @ref url_shortener::app::SystemInfo snapshot, and the
 * `system.info` result serializer (subtask 17). Reads only allowlisted backend
 * names and the explicitly-named non-secret `ServerConfig` subset; never a DSN,
 * path, address, TLS field or the analytics salt (milestone contract C6, threat
 * model T-53).
 */
#include <url_shortener/app/system_info.hpp>

#include <array>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <url_shortener/config/storage_config.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener
{
namespace app
{

namespace
{

///< The fixed placeholder reported for any backend string outside its
///< section's allowlist, so a mis-set value (e.g. a DSN pasted into a `backend`
///< field) is never echoed (threat model T-53).
constexpr std::string_view k_unknown_backend = "unknown";

///< The fixed name reported whenever rate limiting is disabled, regardless of
///< the configured `rate_limit.backend` value.
constexpr std::string_view k_disabled_backend = "disabled";

///< Allowlist of metadata backend names; any other value becomes
///< @ref k_unknown_backend.
constexpr std::array<std::string_view, 3> k_metadata_backends = {
    "inmemory",
    "sqlite",
    "postgres",
};

///< Allowlist of cache backend names.
constexpr std::array<std::string_view, 3> k_cache_backends = {
    "none",
    "inmemory",
    "redis",
};

///< Allowlist of analytics backend names.
constexpr std::array<std::string_view, 4> k_analytics_backends = {
    "noop",
    "inmemory",
    "sqlite",
    "postgres",
};

///< Allowlist of rate-limit backend names (used only when rate limiting is
///< enabled).
constexpr std::array<std::string_view, 2> k_rate_limit_backends = {
    "inmemory",
    "redis",
};

/**
 * @brief Normalizes a configured backend string against a fixed allowlist.
 *
 * @tparam N The allowlist size.
 * @param value The raw configured backend string.
 * @param allowed The allowlist of accepted names for this section.
 * @return @p value when it exactly matches an allowed name; otherwise the
 * fixed placeholder @ref k_unknown_backend. The raw value is never returned
 * when it is not on the allowlist (threat model T-53).
 */
template <std::size_t N>
std::string normalize_backend(const std::string& value,
                              const std::array<std::string_view, N>& allowed)
{
    for (const std::string_view name : allowed) {
        if (value == name) {
            return value;
        }
    }
    return std::string(k_unknown_backend);
}

/**
 * @brief Appends a `"key":<escaped-string>` pair to @p os.
 *
 * @param os The stream to append to.
 * @param key The JSON key (a fixed, non-secret literal).
 * @param value The string value, escaped via `jsonString` before emission.
 */
void append_string_field(std::ostringstream& os,
                         std::string_view key,
                         const std::string& value)
{
    os << '"' << key << "\":" << jsonString(value);
}

/**
 * @brief Appends a `"key":true|false` pair to @p os.
 *
 * @param os The stream to append to.
 * @param key The JSON key (a fixed, non-secret literal).
 * @param value The boolean value.
 */
void append_bool_field(std::ostringstream& os,
                       std::string_view key,
                       bool value)
{
    os << '"' << key << "\":" << (value ? "true" : "false");
}

/**
 * @brief Appends a `"key":<unsigned-number>` pair to @p os.
 *
 * @param os The stream to append to.
 * @param key The JSON key (a fixed, non-secret literal).
 * @param value The unsigned numeric value.
 */
void append_uint_field(std::ostringstream& os,
                       std::string_view key,
                       std::uint32_t value)
{
    os << '"' << key << "\":" << value;
}

/**
 * @brief Returns the process-wide snapshot storage, default-initialized from
 * the in-memory config defaults on first use.
 *
 * @return A reference to the single process snapshot, guarded by
 * @ref process_info_mutex.
 */
SystemInfo& process_info_storage()
{
    static SystemInfo info =
        make_system_info(::StorageConfig {}, ::ServerConfig {});
    return info;
}

/**
 * @brief Returns the mutex guarding @ref process_info_storage.
 * @return A reference to the process-wide snapshot mutex.
 */
std::mutex& process_info_mutex()
{
    static std::mutex mutex;
    return mutex;
}

}  // namespace

SystemInfo make_system_info(const ::StorageConfig& storage_config,
                            const ::ServerConfig& server_config)
{
    SystemInfo info;

#ifdef URL_SHORTENER_VERSION
    info.version = URL_SHORTENER_VERSION;
#else
    info.version = "0.0.0-dev";
#endif

    info.metadata =
        normalize_backend(storage_config.metadata.backend, k_metadata_backends);
    info.cache =
        normalize_backend(storage_config.cache.backend, k_cache_backends);
    info.analytics = normalize_backend(storage_config.analytics.backend,
                                       k_analytics_backends);
    info.rate_limit =
        storage_config.rate_limit.enabled
            ? normalize_backend(storage_config.rate_limit.backend,
                                k_rate_limit_backends)
            : std::string(k_disabled_backend);

    info.persistent =
        (info.metadata == "sqlite" || info.metadata == "postgres");

    info.shortener.base_domain = server_config.shortener_base_domain;
    info.shortener.generated_slug_length =
        server_config.shortener_generated_slug_length;
    info.shortener.default_redirect_type =
        server_config.shortener_default_redirect_type;
    info.shortener.default_expiry_seconds =
        server_config.shortener_default_expiry_seconds;
    info.shortener.allow_private_targets =
        server_config.shortener_allow_private_targets;
    info.shortener.analytics_enabled = server_config.analytics_enabled;
    info.shortener.max_request_body_bytes =
        server_config.max_request_body_bytes;
    info.shortener.max_request_target_length =
        server_config.max_request_target_length;
    info.shortener.request_id_max_length = server_config.request_id_max_length;

    return info;
}

void set_process_system_info(SystemInfo info)
{
    const std::lock_guard<std::mutex> lock(process_info_mutex());
    process_info_storage() = std::move(info);
}

SystemInfo process_system_info()
{
    const std::lock_guard<std::mutex> lock(process_info_mutex());
    return process_info_storage();
}

std::string serializeSystemInfoJson(const SystemInfo& info, bool read_only)
{
    std::ostringstream os;
    os << '{';

    append_string_field(os, "version", info.version);

    os << ",\"backend\":{";
    append_string_field(os, "metadata", info.metadata);
    os << ',';
    append_string_field(os, "cache", info.cache);
    os << ',';
    append_string_field(os, "analytics", info.analytics);
    os << ',';
    append_string_field(os, "rate_limit", info.rate_limit);
    os << '}';

    os << ',';
    append_bool_field(os, "persistent", info.persistent);
    os << ',';
    append_bool_field(os, "read_only", read_only);

    os << ",\"shortener\":{";
    append_string_field(os, "base_domain", info.shortener.base_domain);
    os << ',';
    append_uint_field(os, "generated_slug_length",
                      info.shortener.generated_slug_length);
    os << ',';
    append_string_field(os, "default_redirect_type",
                        info.shortener.default_redirect_type);
    os << ',';
    os << "\"default_expiry_seconds\":";
    if (info.shortener.default_expiry_seconds) {
        os << *info.shortener.default_expiry_seconds;
    }
    else {
        os << "null";
    }
    os << ',';
    append_bool_field(os, "allow_private_targets",
                      info.shortener.allow_private_targets);
    os << ',';
    append_bool_field(os, "analytics_enabled",
                      info.shortener.analytics_enabled);
    os << ',';
    append_uint_field(os, "max_request_body_bytes",
                      info.shortener.max_request_body_bytes);
    os << ',';
    append_uint_field(os, "max_request_target_length",
                      info.shortener.max_request_target_length);
    os << ',';
    append_uint_field(os, "request_id_max_length",
                      info.shortener.request_id_max_length);
    os << '}';

    os << '}';
    return os.str();
}

}  // namespace app
}  // namespace url_shortener
