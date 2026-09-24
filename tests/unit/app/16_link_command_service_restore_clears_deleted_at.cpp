/**
 * @file 16_link_command_service_restore_clears_deleted_at.cpp
 * @brief Unit tests: RestoreLink clears deleted_at and propagates not_found.
 */
#define BOOST_TEST_MODULE LinkCommandServiceRestore
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.h>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

namespace
{
void seedDeletedLink(FakeLinkStore& store, const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://target.example.com/path";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.deleted_at = currentTimestamp();
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    store.seed(link);
}
}  // namespace

/**
 * [Unit][App] RestoreLink clears deleted_at on a soft-deleted link.
 *
 * If this breaks, first check:
 *   - RestoreLink resets deleted_at and writes back via store.update.
 */
BOOST_AUTO_TEST_CASE(restore_clears_deleted_at)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedDeletedLink(store, "back");
    LinkCommandService svc(store, stats, config);

    RestoreLinkCommand cmd;
    cmd.slug = "back";
    const auto result = svc.RestoreLink(cmd);

    BOOST_REQUIRE(result.ok());
    BOOST_CHECK(!result.value->deleted_at.has_value());
    BOOST_CHECK(!store.findBySlug("back")->deleted_at.has_value());
}

/**
 * [Unit][App] RestoreLink on an already-active (non-deleted) link is an
 * idempotent success: the CURRENT implementation resets deleted_at
 * unconditionally, so restoring a link that was never deleted still returns ok
 * with deleted_at empty.
 *
 * If this breaks, first check:
 *   - RestoreLink resets deleted_at without a "was it deleted?" precondition.
 */
BOOST_AUTO_TEST_CASE(restore_non_deleted_link_is_idempotent_success)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();

    Link link;
    link.id = "id-active";
    link.slug = "active";
    link.target_url = "https://target.example.com/path";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    store.seed(link);  // no deleted_at set
    LinkCommandService svc(store, stats, config);

    RestoreLinkCommand cmd;
    cmd.slug = "active";
    const auto result = svc.RestoreLink(cmd);

    BOOST_REQUIRE(result.ok());
    BOOST_CHECK(!result.value->deleted_at.has_value());
    BOOST_CHECK(!store.findBySlug("active")->deleted_at.has_value());
}

/**
 * [Unit][App] Restoring a missing slug returns not_found.
 */
BOOST_AUTO_TEST_CASE(restore_missing_slug_returns_not_found)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    RestoreLinkCommand cmd;
    cmd.slug = "ghost";
    const auto result = svc.RestoreLink(cmd);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}
