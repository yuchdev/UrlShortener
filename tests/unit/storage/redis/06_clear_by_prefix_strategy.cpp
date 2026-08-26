#define BOOST_TEST_MODULE RedisClearByPrefixStrategy
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/redis/redis_cache_serialization.hpp"

BOOST_AUTO_TEST_CASE(prefix_isolated_keyspace) {
    BOOST_TEST(redis_cache::BuildCacheKey("svc-a:link:", "x") != redis_cache::BuildCacheKey("svc-b:link:", "x"));
}
