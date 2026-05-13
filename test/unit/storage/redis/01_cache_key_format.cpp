#define BOOST_TEST_MODULE RedisCacheKeyFormat
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"

BOOST_AUTO_TEST_CASE(builds_expected_key) {
    BOOST_TEST(redis_cache::BuildCacheKey("link:", "abc") == "link:abc");
}
