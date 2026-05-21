#define BOOST_TEST_MODULE RedisSetGetDeleteCommands
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/redis/RedisCacheStore.hpp"

BOOST_AUTO_TEST_CASE(unavailable_backend_maps_to_failure_without_crash) {
    RedisCacheConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 1;
    RedisCacheStore store(cfg);
    CacheError e = CacheError::none;
    const CacheValue v{"https://example", std::nullopt, true, 1};
    BOOST_TEST(!store.Set("abc", v, std::nullopt, &e));
    BOOST_TEST(e == CacheError::unavailable);
    BOOST_TEST(!store.Get("abc", &e).has_value());
    BOOST_TEST(e == CacheError::unavailable);
    BOOST_TEST(!store.Delete("abc", &e));
    BOOST_TEST(e == CacheError::unavailable);
}
