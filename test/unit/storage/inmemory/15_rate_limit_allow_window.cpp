#define BOOST_TEST_MODULE InMemoryRateLimitAllowWindowTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"
BOOST_AUTO_TEST_CASE(rate_limit_window_behavior)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryRateLimiter limiter(clock);
    RateLimitError err = RateLimitError::none;
    BOOST_TEST(limiter.Allow("k", 2, std::chrono::seconds(10), &err).allowed);
    BOOST_TEST(limiter.Allow("k", 2, std::chrono::seconds(10), &err).allowed);
    BOOST_TEST(!limiter.Allow("k", 2, std::chrono::seconds(10), &err).allowed);
    clock.advance(std::chrono::seconds(11));
    BOOST_TEST(limiter.Allow("k", 2, std::chrono::seconds(10), &err).allowed);
    BOOST_TEST(!limiter.Allow("", 1, std::chrono::seconds(10), &err).allowed);
    BOOST_TEST(!limiter.Allow("k2", 0, std::chrono::seconds(10), &err).allowed);
}
