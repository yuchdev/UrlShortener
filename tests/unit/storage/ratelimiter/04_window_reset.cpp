#define BOOST_TEST_MODULE rl_reset
#include <boost/test/unit_test.hpp>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"

BOOST_AUTO_TEST_CASE(first_request_after_window_reset_is_allowed)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryRateLimiter limiter(clock);
    RateLimitError error = RateLimitError::none;

    limiter.Allow("k", 1, std::chrono::seconds(10), &error);
    const auto denied = limiter.Allow("k", 1, std::chrono::seconds(10), &error);
    BOOST_TEST(!denied.allowed);

    clock.advance(std::chrono::seconds(11));
    const auto allowed = limiter.Allow("k", 1, std::chrono::seconds(10), &error);
    BOOST_TEST(allowed.allowed);
}
