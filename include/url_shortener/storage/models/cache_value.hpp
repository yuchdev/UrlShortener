#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

/**
 * @brief Redirect cache payload stored and retrieved via
 * `ICacheStore::Get`/`Set`. Populated from `LinkRecord` fields by
 * the service layer; a cache miss or failure falls through to
 * `IMetadataRepository` (fail-open per the cache-aside contract).
 */
struct CacheValue
{
    ///< Destination URL to redirect to.
    std::string target_url;

    ///< Wall-clock expiry of the link itself (from `LinkRecord`),
    ///< distinct from the per-entry cache TTL set via
    ///< `ICacheStore::Set`. Absent when the link does not expire.
    std::optional<std::chrono::system_clock::time_point> expires_at;

    ///< `true` when the link is enabled and may serve redirects.
    bool is_active = true;

    ///< Optimistic-concurrency version mirrored from `LinkRecord`;
    ///< used to detect stale entries. Absent when the backend does
    ///< not track versions.
    std::optional<uint64_t> version;
};
