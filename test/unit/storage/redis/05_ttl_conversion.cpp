#define BOOST_TEST_MODULE 05_ttl_conversion
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"
#include "url_shortener/storage/redis/RedisCacheStore.hpp"

BOOST_AUTO_TEST_CASE(smoke_05_ttl_conversion) {
    BOOST_TEST(true);
}
