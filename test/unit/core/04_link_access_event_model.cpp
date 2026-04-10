#define BOOST_TEST_MODULE CoreLinkAccessEventModelTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/models/LinkAccessEvent.hpp"

BOOST_AUTO_TEST_CASE(link_access_event_fields_roundtrip)
{
    LinkAccessEvent event{"code", std::chrono::system_clock::time_point{}, 302, true, "client"};
    BOOST_TEST(event.short_code == "code");
    BOOST_TEST(event.status_code == 302);
    BOOST_TEST(event.cache_hit);
    BOOST_TEST(event.client_id.has_value());
}
