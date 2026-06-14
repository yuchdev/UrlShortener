#include "url_shortener/storage/redis/RedisRateLimiter.hpp"

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

#include <algorithm>
#include <limits>

namespace {
constexpr const char* kFixedWindowScript =
    "local current = redis.call('INCR', KEYS[1]) "
    "if current == 1 then redis.call('EXPIRE', KEYS[1], ARGV[2]) end "
    "local ttl = redis.call('TTL', KEYS[1]) "
    "if ttl < 0 then ttl = ARGV[2] end "
    "if current <= tonumber(ARGV[1]) then return {1, current, ttl} else return {0, current, ttl} end";
}

RedisRateLimiter::RedisRateLimiter(RedisRateLimiterConfig config)
    : config_(std::move(config))
{
}

RedisRateLimiter::~RedisRateLimiter()
{
    if (ctx_ != nullptr) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisRateLimiter::EnsureConnected(RateLimitError* error)
{
    if (ctx_ != nullptr && ctx_->err == 0) {
        if (error) *error = RateLimitError::none;
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
        if (error) *error = RateLimitError::unavailable;
        return false;
    }
    redisSetTimeout(ctx_, tv);
    if (error) *error = RateLimitError::none;
    return true;
}

std::string RedisRateLimiter::ComposeKey(const std::string& prefix, const std::string& scope, const std::string& id)
{
    return prefix + scope + ":" + id;
}

RateLimitDecision RedisRateLimiter::Allow(const std::string& key, uint64_t limit, std::chrono::seconds window, RateLimitError* error)
{
    const auto now = std::chrono::system_clock::now();
    if (key.empty()) {
        if (error) *error = RateLimitError::invalid_key;
        return {false, 0, now};
    }
    if (window.count() <= 0) {
        if (error) *error = RateLimitError::invalid_policy;
        return {false, 0, now};
    }
    if (!EnsureConnected(error)) {
        return {false, 0, now};
    }

    const auto redis_key = ComposeKey(config_.key_prefix, config_.scope, key);
    auto* reply = static_cast<redisReply*>(redisCommand(ctx_, "EVAL %s 1 %s %llu %lld",
                                                         kFixedWindowScript,
                                                         redis_key.c_str(),
                                                         static_cast<unsigned long long>(limit),
                                                         static_cast<long long>(window.count())));
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY || reply->elements < 3) {
        if (error) *error = RateLimitError::unavailable;
        if (reply) freeReplyObject(reply);
        return {false, 0, now};
    }

    const bool allowed = reply->element[0]->integer == 1;
    const uint64_t count = static_cast<uint64_t>(std::max<long long>(0, reply->element[1]->integer));
    const auto ttl = std::max<long long>(0, reply->element[2]->integer);
    const uint64_t remaining = (limit == 0 || count >= limit) ? 0 : (limit - count);
    const auto reset_at = now + std::chrono::seconds(ttl);

    freeReplyObject(reply);
    if (error) *error = RateLimitError::none;
    return {allowed && limit > 0, remaining, reset_at};
}
