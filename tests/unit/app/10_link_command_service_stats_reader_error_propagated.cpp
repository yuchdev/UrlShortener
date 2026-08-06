/**
 * @file 10_link_command_service_stats_reader_error_propagated.cpp
 * @brief Unit test: GetLinkStats propagates reader errors unchanged to the
 * caller.
 */
#define BOOST_TEST_MODULE LinkCommandServiceStatsReaderErrorPropagated
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/LinkCommandService.hpp>

#include "AppTestFakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] GetLinkStats propagates invalid_field error from the reader.
 *
 * Scenario:
 *   Given FakeLinkStatsReader forced to return AppErrorCode::invalid_field.
 *   When GetLinkStats is called.
 *   Then result is !ok() with AppErrorCode::invalid_field and error detail
 * preserved.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::GetLinkStats error propagation path.
 *
 * If this breaks, first check:
 *   - GetLinkStats returns the reader's Result directly without silencing
 * errors.
 */
BOOST_AUTO_TEST_CASE(get_link_stats_propagates_reader_invalid_field_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    stats.forceError({AppErrorCode::invalid_field, "invalid_bucket"});

    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkStatsQuery query;
    query.slug = "some-slug";
    query.from = "1700000000";
    query.to = "1700003600";
    query.bucket = "bad";

    const auto result = svc.GetLinkStats(query);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_field);
    BOOST_CHECK(!result.value.has_value());
}

/**
 * [Unit][App] GetLinkStats propagates not_found error from the reader.
 *
 * Scenario:
 *   Given FakeLinkStatsReader forced to return AppErrorCode::not_found.
 *   When GetLinkStats is called.
 *   Then result is !ok() with AppErrorCode::not_found.
 */
BOOST_AUTO_TEST_CASE(get_link_stats_propagates_reader_not_found_error)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    stats.forceError({AppErrorCode::not_found, "Link not found"});

    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkStatsQuery query;
    query.slug = "";
    query.from = "1700000000";
    query.to = "1700003600";
    query.bucket = "day";

    const auto result = svc.GetLinkStats(query);

    BOOST_CHECK(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}
