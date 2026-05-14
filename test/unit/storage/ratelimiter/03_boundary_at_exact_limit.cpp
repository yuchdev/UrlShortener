#define BOOST_TEST_MODULE rl_boundary
#include <boost/test/unit_test.hpp>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryRateLimiter.hpp"

BOOST_AUTO_TEST_CASE(exact_limit_is_allowed_limit_plus_one_is_denied)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryRateLimiter limiter(clock);
    RateLimitError error = RateLimitError::none;

    const auto first = limiter.Allow("k", 2, std::chrono::seconds(60), &error);
    const auto second = limiter.Allow("k", 2, std::chrono::seconds(60), &error);
    const auto third = limiter.Allow("k", 2, std::chrono::seconds(60), &error);

    BOOST_TEST(first.allowed);
    BOOST_TEST(second.allowed);
    BOOST_TEST(!third.allowed);
    BOOST_TEST(second.remaining == 0);
}
