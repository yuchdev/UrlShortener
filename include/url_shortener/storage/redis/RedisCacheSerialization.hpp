#pragma once

#include <optional>
#include <string>

#include "url_shortener/storage/models/CacheValue.hpp"

namespace redis_cache {
inline constexpr int kSchemaVersion = 1;
std::string BuildCacheKey(const std::string& prefix, const std::string& short_code);
std::string SerializeCacheValue(const CacheValue& value);
std::optional<CacheValue> DeserializeCacheValue(const std::string& payload);
} // namespace redis_cache
