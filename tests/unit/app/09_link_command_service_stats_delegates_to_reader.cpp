/**
 * @file 09_link_command_service_stats_delegates_to_reader.cpp
 * @brief Unit test: GetLinkStats delegates entirely to ILinkStatsReader and
 * returns the view it produces.
 */
#define BOOST_TEST_MODULE LinkCommandServiceStatsDelegatesToReader
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

/**
 * [Unit][App] GetLinkStats returns the view produced by ILinkStatsReader.
 *
 * Scenario:
 *   Given FakeLinkStatsReader (returns a canned view echoing the query).
 *   When GetLinkStats is called with a valid-looking query.
 *   Then result is ok() and slug/from/to/bucket match the query.
 *
 * API/Feature covered:
 *   - app::LinkCommandService::GetLinkStats delegation contract.
 *
 * If this breaks, first check:
 *   - GetLinkStats passes the query through to stats_reader_.read() unchanged.
 *   - Result propagation from reader to caller.
 */
BOOST_AUTO_TEST_CASE(get_link_stats_delegates_to_reader_and_returns_view)
{
    FakeLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    GetLinkStatsQuery query;
    query.slug = "campaign-slug";
    query.from = "1700000000";
    query.to = "1700003600";
    query.bucket = "hour";

    const auto result = svc.GetLinkStats(query);

    BOOST_REQUIRE(result.ok());
    BOOST_REQUIRE(result.value.has_value());
    const auto& view = *result.value;

    BOOST_CHECK_EQUAL(view.slug, "campaign-slug");
    BOOST_CHECK_EQUAL(view.from, "1700000000");
    BOOST_CHECK_EQUAL(view.to, "1700003600");
    BOOST_CHECK_EQUAL(view.bucket, "hour");
}
