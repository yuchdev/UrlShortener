#define BOOST_TEST_MODULE RedisFailOpenErrorMapping
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/redis/RedisCacheStore.hpp"

BOOST_AUTO_TEST_CASE(connection_error_classifies_as_unavailable) {
    RedisCacheConfig cfg;
    cfg.host = "invalid.invalid";
    cfg.port = 6379;
    RedisCacheStore store(cfg);
    CacheError e = CacheError::none;
    auto got = store.Get("k", &e);
    BOOST_TEST(!got.has_value());
    BOOST_TEST(e == CacheError::unavailable);
}
