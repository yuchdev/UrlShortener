/**
 * @file 05_link_command_service_create_explicit_slug.cpp
 * @brief Unit test: CreateLink with a valid explicit slug uses it verbatim.
 */
#define BOOST_TEST_MODULE LinkCommandServiceCreateExplicitSlug
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] CreateLink with an explicit valid slug stores and returns that
 * slug.
 *
 * Scenario:
 *   Given an empty FakeLinkStore and slug "promo2026" (valid, not reserved).
 *   When CreateLink is called with slug = "promo2026".
 *   Then the result is ok() and result.value->slug == "promo2026".
 *
 * API/Feature covered:
 *   - app::LinkCommandService::chooseSlug explicit slug path.
 *
 * If this breaks, first check:
 *   - isValidSlug() acceptance of alphanumeric + hyphen slugs.
 *   - chooseSlug() returns the explicit slug when all guards pass.
 */
BOOST_AUTO_TEST_CASE(create_link_explicit_slug_preserved_in_result)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand cmd;
    cmd.target_url = "https://example.com/page";
    cmd.slug = std::string {"promo2026"};

    const auto result = svc.CreateLink(cmd);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK_EQUAL(result.value->slug, "promo2026");
    BOOST_CHECK_EQUAL(result.value->target_url, "https://example.com/page");
    BOOST_CHECK_EQUAL(result.value->status, LinkStatus::active);
}

/**
 * [Unit][App] CreateLink with explicit slug stores it in the repository.
 *
 * Scenario:
 *   After CreateLink with slug "persist-me", a subsequent GetLink by slug
 *   finds the same link in the same store.
 */
BOOST_AUTO_TEST_CASE(create_link_explicit_slug_persisted_to_store)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    CreateLinkCommand create_cmd;
    create_cmd.target_url = "https://example.com/persisted";
    create_cmd.slug = std::string {"persist-me"};
    BOOST_REQUIRE(svc.CreateLink(create_cmd).ok());

    GetLinkQuery get_query;
    get_query.by = GetLinkBy::slug;
    get_query.value = "persist-me";

    const auto get_result = svc.GetLink(get_query);
    BOOST_REQUIRE(get_result.ok());
    BOOST_REQUIRE(get_result.value.has_value());
    BOOST_CHECK_EQUAL(get_result.value->target_url,
                      "https://example.com/persisted");
}
