/**
 * @file 15_link_command_service_set_enabled_toggle.cpp
 * @brief Unit tests: SetLinkEnabled routes both enable and disable through one
 * method and propagates not_found.
 */
#define BOOST_TEST_MODULE LinkCommandServiceSetEnabled
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.h>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

namespace
{
void seedLinkWithEnabled(FakeLinkStore& store,
                         const std::string& slug,
                         const bool enabled)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://target.example.com/path";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = enabled;
    link.redirect_type = RedirectType::temporary;
    store.seed(link);
}
}  // namespace

/**
 * [Unit][App] SetLinkEnabled with enabled=false disables an active link.
 *
 * If this breaks, first check:
 *   - SetLinkEnabled assigns command.enabled and writes back.
 */
BOOST_AUTO_TEST_CASE(set_enabled_false_disables)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLinkWithEnabled(store, "toggle", true);
    LinkCommandService svc(store, stats, config);

    SetLinkEnabledCommand cmd;
    cmd.slug = "toggle";
    cmd.enabled = false;
    const auto result = svc.SetLinkEnabled(cmd);

    BOOST_REQUIRE(result.ok());
    BOOST_CHECK_EQUAL(result.value->enabled, false);
    BOOST_CHECK_EQUAL(store.findBySlug("toggle")->enabled, false);
}

/**
 * [Unit][App] SetLinkEnabled with enabled=true enables a disabled link.
 */
BOOST_AUTO_TEST_CASE(set_enabled_true_enables)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLinkWithEnabled(store, "toggle", false);
    LinkCommandService svc(store, stats, config);

    SetLinkEnabledCommand cmd;
    cmd.slug = "toggle";
    cmd.enabled = true;
    const auto result = svc.SetLinkEnabled(cmd);

    BOOST_REQUIRE(result.ok());
    BOOST_CHECK_EQUAL(result.value->enabled, true);
    BOOST_CHECK_EQUAL(store.findBySlug("toggle")->enabled, true);
}

/**
 * [Unit][App] Toggling a missing slug returns not_found.
 */
BOOST_AUTO_TEST_CASE(set_enabled_missing_slug_returns_not_found)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    SetLinkEnabledCommand cmd;
    cmd.slug = "ghost";
    cmd.enabled = true;
    const auto result = svc.SetLinkEnabled(cmd);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}
