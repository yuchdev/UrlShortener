#define BOOST_TEST_MODULE rl_invalid
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"
#include "url_shortener/core/clock.hpp"
BOOST_AUTO_TEST_CASE(case1){ SystemClock c; InMemoryRateLimiter l(c); RateLimitError e=RateLimitError::none; auto a=l.Allow("",1,std::chrono::seconds(1),&e); BOOST_TEST(!a.allowed); BOOST_TEST(e==RateLimitError::invalid_key); auto b=l.Allow("k",1,std::chrono::seconds(0),&e); BOOST_TEST(!b.allowed); BOOST_TEST(e==RateLimitError::invalid_policy); auto z=l.Allow("k",0,std::chrono::seconds(1),&e); BOOST_TEST(!z.allowed);} 
