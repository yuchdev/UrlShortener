/**
 * @file 02_link_command_service_create_invalid_url.cpp
 * @brief Unit test: CreateLink with a non-HTTP(S) or malformed URL returns
 * invalid_url.
 */
#define BOOST_TEST_MODULE LinkCommandServiceCreateInvalidUrl
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] CreateLink rejects a non-URL string with invalid_url error.
 *
 * Scenario:
 *   Given an empty FakeLinkStore.
 *   When CreateLink is called with a bare word that is not an absolute URL.
 *   Then the result is !ok() with AppErrorCode::invalid_url.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::CreateLink URL validation guard.
 *
 * If this breaks, first check:
 *   - normalizeTargetUrl rejection path for non-http/https inputs.
 */
BOOST_AUTO_TEST_CASE(create_link_bare_word_returns_invalid_url_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "not-a-url-at-all";

    const auto result = svc.CreateLink(cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_url);
    BOOST_CHECK(!result.value.has_value());
}

/**
 * [Unit][App] CreateLink rejects an ftp:// URL with invalid_url error.
 *
 * Scenario:
 *   Given only http/https are accepted target schemes.
 *   When CreateLink is called with "ftp://files.example.com".
 *   Then the result is !ok() with AppErrorCode::invalid_url.
 */
BOOST_AUTO_TEST_CASE(create_link_ftp_scheme_returns_invalid_url_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "ftp://files.example.com/pub";

    const auto result = svc.CreateLink(cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_url);
}

/**
 * [Unit][App] CreateLink rejects an empty URL with invalid_url error.
 */
BOOST_AUTO_TEST_CASE(create_link_empty_url_returns_invalid_url_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "";

    const auto result = svc.CreateLink(cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_url);
}
