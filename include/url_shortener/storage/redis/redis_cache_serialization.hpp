#pragma once

#include <optional>
#include <string>

#include "url_shortener/storage/models/cache_value.hpp"

/**
 * @brief Serialization helpers for `CacheValue` payloads stored in Redis.
 * The format is a pipe-delimited string versioned with `kSchemaVersion`
 * (e.g. `v=1|a=1|e=<epoch>|r=<ver>|n=<len>|u=<url>`). `DeserializeCacheValue`
 * returns `std::nullopt` on any format mismatch so stale or corrupt entries
 * are treated as cache misses rather than errors.
 */
namespace redis_cache
{

///< Version tag embedded in every serialized payload. Increment this when
///< the format changes so that old entries are automatically rejected by
///< `DeserializeCacheValue` (treated as cache misses, not errors).
inline constexpr int kSchemaVersion = 1;

/**
 * @brief Builds a Redis key by concatenating `prefix` and `short_code`.
 * @param prefix Namespace prefix (e.g. `"link:"`).
 * @param short_code Slug or other per-entry identifier.
 * @return `prefix + short_code`.
 */
std::string BuildCacheKey(const std::string& prefix,
                          const std::string& short_code);

/**
 * @brief Encodes a `CacheValue` as a versioned pipe-delimited string
 * suitable for storage as a Redis string value.
 * @param value Value to encode; all fields (including optional ones) are
 * included in the payload.
 * @return The serialized payload string.
 */
std::string SerializeCacheValue(const CacheValue& value);

/**
 * @brief Attempts to decode a serialized `CacheValue` payload.
 * @param payload String previously produced by `SerializeCacheValue`.
 * @return The decoded `CacheValue` on success; `std::nullopt` if the
 * payload does not begin with the expected version tag (`v=1`), has
 * fewer than six pipe-separated fields, contains a malformed field, or
 * if the `target_url` length does not match its declared `n=` prefix.
 */
std::optional<CacheValue> DeserializeCacheValue(const std::string& payload);

}  // namespace redis_cache
