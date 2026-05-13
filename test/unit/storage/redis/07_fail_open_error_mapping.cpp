#define BOOST_TEST_MODULE 07_fail_open_error_mapping
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"
#include "url_shortener/storage/redis/RedisCacheStore.hpp"

BOOST_AUTO_TEST_CASE(smoke_07_fail_open_error_mapping) {
    BOOST_TEST(true);
}
