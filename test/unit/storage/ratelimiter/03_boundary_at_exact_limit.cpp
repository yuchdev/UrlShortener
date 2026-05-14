#define BOOST_TEST_MODULE rl_boundary
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"
#include "url_shortener/core/clock.hpp"
BOOST_AUTO_TEST_CASE(case1){ SystemClock c; InMemoryRateLimiter l(c); RateLimitError e; auto a=l.Allow("k",2,std::chrono::seconds(60),&e); auto b=l.Allow("k",2,std::chrono::seconds(60),&e); auto c3=l.Allow("k",2,std::chrono::seconds(60),&e); BOOST_TEST(a.allowed&&b.allowed&&!c3.allowed);} 
