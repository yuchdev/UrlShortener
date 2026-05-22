#define BOOST_TEST_MODULE InMemoryCacheTtlLazyEvictionTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
BOOST_AUTO_TEST_CASE(cache_ttl_lazy_eviction_and_zero_ttl_rule)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryCacheStore cache(clock);
    BOOST_TEST(cache.Set("k", {"https://a", std::nullopt, true, 1}, std::chrono::seconds(10)));
    BOOST_TEST(cache.Get("k").has_value());
    clock.advance(std::chrono::seconds(11));
    BOOST_TEST(!cache.Get("k").has_value());
    BOOST_TEST(cache.Set("zero", {"https://b", std::nullopt, true, 1}, std::chrono::seconds(0)));
    BOOST_TEST(!cache.Get("zero").has_value());
}
