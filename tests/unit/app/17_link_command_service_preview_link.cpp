/**
 * @file 17_link_command_service_preview_link.cpp
 * @brief Unit tests: PreviewLink resolves by slug or id, exposes soft-deleted
 * records (unlike the redirect path), and propagates not_found on both lookup
 * branches.
 */
#define BOOST_TEST_MODULE LinkCommandServicePreview
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.h>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

namespace
{
/// Seed a fully-populated active link and return the seeded copy.
Link seedLink(FakeLinkStore& store, const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://target.example.com/path";
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::permanent;
    store.seed(link);
    return link;
}
}  // namespace

/**
 * [Unit][App] PreviewLink by slug returns the view for an active link with
 * status resolved to active.
 *
 * If this breaks, first check:
 *   - PreviewLink's findBySlug branch and toView()/resolveLinkStatus mapping.
 */
BOOST_AUTO_TEST_CASE(preview_by_slug_returns_active_view)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLink(store, "peek");
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = "peek";

    const auto result = svc.PreviewLink(query);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK_EQUAL(result.value->slug, "peek");
    BOOST_CHECK_EQUAL(result.value->id, "id-peek");
    BOOST_CHECK_EQUAL(result.value->target_url,
                      "https://target.example.com/path");
    BOOST_CHECK_EQUAL(result.value->redirect_type, RedirectType::permanent);
    BOOST_CHECK_EQUAL(result.value->status, LinkStatus::active);
}

/**
 * [Unit][App] PreviewLink by id resolves through findById (the other lookup
 * branch) and returns the matching view.
 *
 * If this breaks, first check:
 *   - PreviewLink's `query.by == GetLinkBy::id` findById branch.
 */
BOOST_AUTO_TEST_CASE(preview_by_id_returns_view)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    seedLink(store, "byid");
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::id;
    query.value = "id-byid";

    const auto result = svc.PreviewLink(query);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK_EQUAL(result.value->id, "id-byid");
    BOOST_CHECK_EQUAL(result.value->slug, "byid");
}

/**
 * [Unit][App] PreviewLink exposes a soft-deleted record: unlike the redirect
 * fast path, preview returns the link and reports status=deleted so an operator
 * can inspect a soft-deleted slug before restoring it.
 *
 * If this breaks, first check:
 *   - PreviewLink does NOT filter on deleted_at.
 *   - resolveLinkStatus returns deleted when deleted_at is set.
 */
BOOST_AUTO_TEST_CASE(preview_returns_soft_deleted_link_with_deleted_status)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    Link seeded = seedLink(store, "gone");
    seeded.deleted_at = currentTimestamp();
    store.seed(seeded);
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = "gone";

    const auto result = svc.PreviewLink(query);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    BOOST_CHECK(result.value->deleted_at.has_value());
    BOOST_CHECK_EQUAL(result.value->status, LinkStatus::deleted);
}

/**
 * [Unit][App] PreviewLink on a missing slug returns not_found.
 */
BOOST_AUTO_TEST_CASE(preview_missing_slug_returns_not_found)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::slug;
    query.value = "ghost";

    const auto result = svc.PreviewLink(query);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}

/**
 * [Unit][App] PreviewLink on a missing id returns not_found (exercises the
 * findById branch reaching the not_found path).
 */
BOOST_AUTO_TEST_CASE(preview_missing_id_returns_not_found)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkQuery query;
    query.by = GetLinkBy::id;
    query.value = "id-ghost";

    const auto result = svc.PreviewLink(query);

    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}
