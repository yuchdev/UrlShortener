#define BOOST_TEST_MODULE RedisTtlConversion
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"

BOOST_AUTO_TEST_CASE(zero_ttl_behavior_documented_by_serialization_stability) {
    CacheValue v{"https://x", std::nullopt, true, std::nullopt};
    auto s = redis_cache::SerializeCacheValue(v);
    auto d = redis_cache::DeserializeCacheValue(s);
    BOOST_REQUIRE(d.has_value());
    BOOST_TEST(!d->expires_at.has_value());
}
