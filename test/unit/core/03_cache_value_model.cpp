#define BOOST_TEST_MODULE CoreCacheValueModelTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/models/CacheValue.hpp"

BOOST_AUTO_TEST_CASE(cache_value_active_and_expiring)
{
    CacheValue active{"https://example.com", std::nullopt, true, 1};
    BOOST_TEST(active.is_active);
    BOOST_TEST(active.expires_at.has_value() == false);

    CacheValue expiring{"https://example.com", std::chrono::system_clock::time_point{}, true, 2};
    BOOST_TEST(expiring.expires_at.has_value());
}
