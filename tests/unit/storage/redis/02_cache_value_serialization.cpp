#define BOOST_TEST_MODULE RedisCacheValueSerialization
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"

BOOST_AUTO_TEST_CASE(includes_schema_version_and_url_size) {
    CacheValue v{"https://x/y?z=1", std::nullopt, true, 5};
    auto s = redis_cache::SerializeCacheValue(v);
    BOOST_TEST(s.find("v=1") != std::string::npos);
    BOOST_TEST(s.find("n=15") != std::string::npos);
}
