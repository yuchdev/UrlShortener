#define BOOST_TEST_MODULE rl_error
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/redis/RedisRateLimiter.hpp"
BOOST_AUTO_TEST_CASE(case1){ RedisRateLimiter l({"127.0.0.1", 1}); RateLimitError e=RateLimitError::none; auto d=l.Allow("k",1,std::chrono::seconds(1),&e); BOOST_TEST(!d.allowed); BOOST_TEST(e==RateLimitError::unavailable);} 
