#define BOOST_TEST_MODULE rl_key_format
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/redis_rate_limiter.hpp"
BOOST_AUTO_TEST_CASE(case1){ BOOST_TEST(RedisRateLimiter::ComposeKey("ratelimit:","global","abc")=="ratelimit:global:abc"); }
