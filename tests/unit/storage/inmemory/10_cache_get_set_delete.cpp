#define BOOST_TEST_MODULE InMemoryCacheGetSetDeleteTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
BOOST_AUTO_TEST_CASE(cache_get_set_delete)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryCacheStore cache(clock);
    CacheValue v{"https://a", std::nullopt, true, 1};
    BOOST_TEST(cache.Set("k", v, std::nullopt));
    auto got = cache.Get("k");
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(got->target_url == "https://a");
    BOOST_TEST(cache.Delete("k"));
    BOOST_TEST(!cache.Get("k").has_value());
    BOOST_TEST(cache.Delete("missing"));
}
