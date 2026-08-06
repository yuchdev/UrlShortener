/**
 * @file 03_link_command_service_create_reserved_slug.cpp
 * @brief Unit test: CreateLink with a system-reserved slug is rejected.
 */
#define BOOST_TEST_MODULE LinkCommandServiceCreateReservedSlug
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/LinkCommandService.hpp>

#include "AppTestFakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] CreateLink rejects "health" slug with reserved_slug error.
 *
 * Scenario:
 *   Given the reserved slug denylist includes "health".
 *   When CreateLink is called with slug = "health".
 *   Then the result is !ok() with AppErrorCode::reserved_slug.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::chooseSlug reserved-slug guard.
 *
 * If this breaks, first check:
 *   - isReservedSlug() denylist and case normalization.
 *   - chooseSlug() control flow for explicit slug path.
 */
BOOST_AUTO_TEST_CASE(create_link_health_slug_returns_reserved_slug_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com";
    cmd.slug = std::string {"health"};

    const auto result = svc.CreateLink(cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::reserved_slug);
    BOOST_CHECK(!result.value.has_value());
}

/**
 * [Unit][App] CreateLink rejects "api" slug with reserved_slug error.
 */
BOOST_AUTO_TEST_CASE(create_link_api_slug_returns_reserved_slug_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com";
    cmd.slug = std::string {"api"};

    const auto result = svc.CreateLink(cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::reserved_slug);
}

/**
 * [Unit][App] CreateLink rejects "STATS" (case-insensitive reserved slug).
 */
BOOST_AUTO_TEST_CASE(
    create_link_uppercase_reserved_slug_returns_reserved_slug_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com";
    cmd.slug = std::string {"STATS"};

    const auto result = svc.CreateLink(cmd);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::reserved_slug);
}
