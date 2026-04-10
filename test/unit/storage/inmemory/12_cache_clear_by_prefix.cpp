#define BOOST_TEST_MODULE InMemoryCacheClearByPrefixTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
BOOST_AUTO_TEST_CASE(clear_by_prefix_only_removes_matches)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryCacheStore cache(clock);
    cache.Set("a:1", {"https://a", std::nullopt, true, 1}, std::nullopt);
    cache.Set("a:2", {"https://a", std::nullopt, true, 1}, std::nullopt);
    cache.Set("b:1", {"https://b", std::nullopt, true, 1}, std::nullopt);
    BOOST_TEST(cache.ClearByPrefix("a:"));
    BOOST_TEST(!cache.Get("a:1").has_value());
    BOOST_TEST(cache.Get("b:1").has_value());
    BOOST_TEST(cache.ClearByPrefix("none"));
}
