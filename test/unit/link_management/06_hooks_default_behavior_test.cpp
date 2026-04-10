/** @file 06_hooks_default_behavior_test.cpp @brief Unit test: default password/rate-limit hooks are pass-through. */
#define BOOST_TEST_MODULE LinkManagementHookDefaultsTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

/**
 * [Unit][Link Management] Default hooks pass requests through.
 *
 * Scenario:
 *   Given default password/rate-limit hook implementations.
 *   When passwordGuardCheck() and rateLimitGuardAllow() are called.
 *   Then both hooks allow the request.
 *
 * API/Feature covered:
 *   - Stage 2 no-op extensibility hooks.
 *
 * If this breaks:
 *   - Redirect path may fail before custom guards are even introduced.
 *   - First check default hook wiring and return values.
 */
BOOST_AUTO_TEST_CASE(password_and_rate_limit_hooks_default_pass)
{
    Link link;
    http::request<http::string_body> req{http::verb::get, "/abc", 11};
    BOOST_TEST(passwordGuardCheck(link, req));
    BOOST_TEST(rateLimitGuardAllow(link, req));
}
