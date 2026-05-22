#define BOOST_TEST_MODULE RedisCacheValueDeserialization
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"

BOOST_AUTO_TEST_CASE(roundtrip_reconstructs_value) {
    CacheValue v{"https://éxample.com/☃", std::chrono::system_clock::time_point{std::chrono::seconds{1710000000}}, false, 7};
    auto d = redis_cache::DeserializeCacheValue(redis_cache::SerializeCacheValue(v));
    BOOST_REQUIRE(d.has_value());
    BOOST_TEST(d->target_url == v.target_url);
    BOOST_TEST(d->is_active == false);
    BOOST_TEST(d->version.value() == 7U);
}

BOOST_AUTO_TEST_CASE(malformed_fails_safely) {
    BOOST_TEST(!redis_cache::DeserializeCacheValue("bad").has_value());
}
