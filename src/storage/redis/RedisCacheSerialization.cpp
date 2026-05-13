#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"

#include <chrono>
#include <sstream>
#include <vector>

namespace redis_cache {
namespace {
std::vector<std::string> Split(const std::string& in, char delim)
{
    std::vector<std::string> out;
    std::stringstream ss(in);
    std::string part;
    while (std::getline(ss, part, delim)) {
        out.push_back(part);
    }
    return out;
}
} // namespace

std::string BuildCacheKey(const std::string& prefix, const std::string& short_code) { return prefix + short_code; }

std::string SerializeCacheValue(const CacheValue& value)
{
    const auto exp = value.expires_at.has_value()
        ? std::to_string(std::chrono::duration_cast<std::chrono::seconds>(value.expires_at->time_since_epoch()).count())
        : std::string();
    const auto ver = value.version.has_value() ? std::to_string(*value.version) : std::string();
    return "v=" + std::to_string(kSchemaVersion)
        + "|a=" + (value.is_active ? "1" : "0")
        + "|e=" + exp
        + "|r=" + ver
        + "|n=" + std::to_string(value.target_url.size())
        + "|u=" + value.target_url;
}

std::optional<CacheValue> DeserializeCacheValue(const std::string& payload)
{
    const auto parts = Split(payload, '|');
    if (parts.size() < 6) return std::nullopt;
    if (parts[0] != "v=1") return std::nullopt;
    if (parts[1] != "a=0" && parts[1] != "a=1") return std::nullopt;
    if (parts[4].rfind("n=", 0) != 0 || parts[5].rfind("u=", 0) != 0) return std::nullopt;

    CacheValue value;
    value.is_active = (parts[1] == "a=1");
    if (parts[2].rfind("e=", 0) != 0 || parts[3].rfind("r=", 0) != 0) return std::nullopt;
    const auto exp = parts[2].substr(2);
    if (!exp.empty()) {
        try {
            const auto s = std::stoll(exp);
            value.expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{s}};
        } catch (...) { return std::nullopt; }
    }
    const auto ver = parts[3].substr(2);
    if (!ver.empty()) {
        try { value.version = static_cast<uint64_t>(std::stoull(ver)); } catch (...) { return std::nullopt; }
    }
    size_t n = 0;
    try { n = static_cast<size_t>(std::stoull(parts[4].substr(2))); } catch (...) { return std::nullopt; }
    value.target_url = parts[5].substr(2);
    if (value.target_url.size() != n) return std::nullopt;
    return value;
}
} // namespace redis_cache
