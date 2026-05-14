#define BOOST_TEST_MODULE rl_reset
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"

BOOST_AUTO_TEST_CASE(case1){ ManualClock clk(std::chrono::system_clock::time_point{}); InMemoryRateLimiter l(clk); RateLimitError e; l.Allow("k",1,std::chrono::seconds(10),&e); auto d=l.Allow("k",1,std::chrono::seconds(10),&e); BOOST_TEST(!d.allowed); clk.advance(std::chrono::seconds(11)); auto e2=l.Allow("k",1,std::chrono::seconds(10),&e); BOOST_TEST(e2.allowed);} 
