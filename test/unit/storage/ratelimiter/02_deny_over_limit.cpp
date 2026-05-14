#define BOOST_TEST_MODULE rl_deny_over
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"
#include "url_shortener/core/clock.hpp"
BOOST_AUTO_TEST_CASE(case1){ SystemClock c; InMemoryRateLimiter l(c); RateLimitError e; l.Allow("k",1,std::chrono::seconds(60),&e); auto d=l.Allow("k",1,std::chrono::seconds(60),&e); BOOST_TEST(!d.allowed); BOOST_TEST(d.remaining==0);} 
