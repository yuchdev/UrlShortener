#define BOOST_TEST_MODULE RedisSchemaVersionCompatibility
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/RedisCacheSerialization.hpp"

BOOST_AUTO_TEST_CASE(unknown_schema_version_rejected) {
    auto p = std::string("v=2|a=1|e=|r=|n=3|u=abc");
    BOOST_TEST(!redis_cache::DeserializeCacheValue(p).has_value());
}
