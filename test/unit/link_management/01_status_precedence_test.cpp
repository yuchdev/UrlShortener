#define BOOST_TEST_MODULE LinkManagementStatusPrecedenceTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(status_precedence_deleted_disabled_expired_active)
{
    Link link;
    link.enabled = true;
    link.expires_at = std::string{"2099-01-01T00:00:00Z"};
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::active);

    link.expires_at = std::string{"2000-01-01T00:00:00Z"};
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::expired);

    link.enabled = false;
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::disabled);

    link.deleted_at = std::string{"2026-01-01T00:00:00Z"};
    BOOST_TEST(resolveLinkStatus(link) == LinkStatus::deleted);
}
