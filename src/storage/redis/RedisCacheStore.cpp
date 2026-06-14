#include "url_shortener/storage/redis/RedisCacheStore.hpp"

// struct timeval lives in <winsock2.h> on Windows and <sys/time.h> on POSIX.
#if defined(_WIN32)
#include <winsock2.h>
#else
#include <sys/time.h>
#endif

#if __has_include(<hiredis/hiredis.h>)
#include <hiredis/hiredis.h>
#elif __has_include(<hiredis.h>)
#include <hiredis.h>
#else
#error "hiredis headers not found"
#endif

#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"

RedisCacheStore::RedisCacheStore(RedisCacheConfig config)
    : config_(std::move(config))
{
}

RedisCacheStore::~RedisCacheStore()
{
    if (ctx_ != nullptr) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisCacheStore::EnsureConnected(CacheError* error)
{
    if (ctx_ != nullptr && ctx_->err == 0) {
        if (error) *error = CacheError::none;
        return true;
    }
    if (ctx_ != nullptr) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
    timeval tv{static_cast<long>(config_.connect_timeout.count() / 1000),
               static_cast<long>((config_.connect_timeout.count() % 1000) * 1000)};
    ctx_ = redisConnectWithTimeout(config_.host.c_str(), config_.port, tv);
    if (ctx_ == nullptr || ctx_->err != 0) {
        if (error) *error = CacheError::unavailable;
        return false;
    }
    redisSetTimeout(ctx_, tv);
    if (error) *error = CacheError::none;
    return true;
}

std::optional<CacheValue> RedisCacheStore::Get(const std::string& key, CacheError* error)
{
    if (!EnsureConnected(error)) return std::nullopt;
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "GET %s", FullKey(key).c_str()));
    if (reply == nullptr) {
        if (error) *error = MapRedisError(false);
        return std::nullopt;
    }
    std::optional<CacheValue> out = std::nullopt;
    if (reply->type == REDIS_REPLY_STRING) out = redis_cache::DeserializeCacheValue(reply->str);
    if (error) *error = CacheError::none;
    freeReplyObject(reply);
    return out;
}

bool RedisCacheStore::Set(const std::string& key, const CacheValue& value, std::optional<std::chrono::seconds> ttl, CacheError* error)
{
    if (!EnsureConnected(error)) return false;
    const auto payload = redis_cache::SerializeCacheValue(value);
    const auto eff_ttl = EffectiveTtl(ttl);
    redisReply* reply = nullptr;
    if (eff_ttl.has_value() && eff_ttl->count() > 0) {
        reply = static_cast<redisReply*>(redisCommand(ctx_, "SETEX %s %lld %b", FullKey(key).c_str(), static_cast<long long>(eff_ttl->count()), payload.data(), payload.size()));
    } else {
        reply = static_cast<redisReply*>(redisCommand(ctx_, "SET %s %b", FullKey(key).c_str(), payload.data(), payload.size()));
    }
    if (reply == nullptr) {
        if (error) *error = MapRedisError(false);
        return false;
    }
    const bool ok = reply->type == REDIS_REPLY_STATUS;
    if (error) *error = ok ? CacheError::none : CacheError::permanent_failure;
    freeReplyObject(reply);
    return ok;
}

bool RedisCacheStore::Delete(const std::string& key, CacheError* error)
{
    if (!EnsureConnected(error)) return false;
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "DEL %s", FullKey(key).c_str()));
    if (reply == nullptr) {
        if (error) *error = MapRedisError(false);
        return false;
    }
    if (error) *error = CacheError::none;
    freeReplyObject(reply);
    return true;
}

bool RedisCacheStore::ClearByPrefix(const std::string& prefix, CacheError* error)
{
    if (!EnsureConnected(error)) return false;
    auto* keys = static_cast<redisReply*>(redisCommand(ctx_, "KEYS %s*", FullPrefix(prefix).c_str()));
    if (keys == nullptr || keys->type != REDIS_REPLY_ARRAY) {
        if (error) *error = CacheError::unavailable;
        if (keys) freeReplyObject(keys);
        return false;
    }
    for (size_t i = 0; i < keys->elements; ++i) {
        redisCommand(ctx_, "DEL %s", keys->element[i]->str);
    }
    freeReplyObject(keys);
    if (error) *error = CacheError::none;
    return true;
}

CacheError RedisCacheStore::MapRedisError(bool) { return CacheError::unavailable; }
std::optional<std::chrono::seconds> RedisCacheStore::EffectiveTtl(std::optional<std::chrono::seconds> ttl) const { return ttl.has_value() ? ttl : config_.default_ttl; }
std::string RedisCacheStore::FullKey(const std::string& key) const { return redis_cache::BuildCacheKey(config_.key_prefix, key); }
std::string RedisCacheStore::FullPrefix(const std::string& prefix) const { return redis_cache::BuildCacheKey(config_.key_prefix, prefix); }
