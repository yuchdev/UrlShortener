#define BOOST_TEST_MODULE LinkManagementHookDefaultsTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(password_and_rate_limit_hooks_default_pass)
{
    Link link;
    http::request<http::string_body> req{http::verb::get, "/abc", 11};
    BOOST_TEST(passwordGuardCheck(link, req));
    BOOST_TEST(rateLimitGuardAllow(link, req));
}
