/**
 * @file 12_serialize_link_stats_json_fields.cpp
 * @brief Unit test: serializeLinkStatsJson emits all required JSON fields.
 */
#define BOOST_TEST_MODULE SerializeLinkStatsJsonFields
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>

using namespace url_shortener::app;

namespace
{

LinkStatsView makeSampleStats()
{
    LinkStatsView s;
    s.slug = "stats-slug";
    s.total_attempts = 100;
    s.successful_redirects = 90;
    s.from = "1700000000";
    s.to = "1700003600";
    s.bucket = "hour";
    return s;
}

}  // namespace

/**
 * [Unit][App] serializeLinkStatsJson contains required top-level fields.
 *
 * Scenario:
 *   Given a LinkStatsView with total_attempts=100, successful_redirects=90.
 *   When serializeLinkStatsJson() is called.
 *   Then the returned string contains slug, total_attempts,
 * successful_redirects, from, to, bucket, empty status-code and domain maps,
 * and empty time buckets.
 *
 * API/Feature covered:
 *   - app::serializeLinkStatsJson() completeness and correctness.
 *
 * If this breaks, first check:
 *   - Field names in serializeLinkStatsJson ostringstream assembly.
 *   - Numeric serialization of attempt counters.
 */
BOOST_AUTO_TEST_CASE(serialize_link_stats_json_contains_required_fields)
{
    const auto stats = makeSampleStats();
    const auto json = serializeLinkStatsJson(stats);

    BOOST_CHECK(json.find("\"slug\":\"stats-slug\"") != std::string::npos);
    BOOST_CHECK(json.find("\"total_attempts\":100") != std::string::npos);
    BOOST_CHECK(json.find("\"successful_redirects\":90") != std::string::npos);
    BOOST_CHECK(json.find("\"bucket\":\"hour\"") != std::string::npos);
    BOOST_CHECK(json.find("\"attempts_by_status_code\":{}")
                != std::string::npos);
    BOOST_CHECK(json.find("\"attempts_by_domain\":{}") != std::string::npos);
    BOOST_CHECK(json.find("\"time_buckets\":[]") != std::string::npos);
}

/**
 * [Unit][App] serializeLinkStatsJson emits status code counts correctly.
 */
BOOST_AUTO_TEST_CASE(serialize_link_stats_json_status_code_counts)
{
    LinkStatsView stats = makeSampleStats();
    stats.status_code_counts.push_back({302, 80});
    stats.status_code_counts.push_back({301, 10});

    const auto json = serializeLinkStatsJson(stats);

    BOOST_CHECK(json.find("\"attempts_by_status_code\":{")
                != std::string::npos);
    BOOST_CHECK(json.find("\"302\":80") != std::string::npos);
    BOOST_CHECK(json.find("\"301\":10") != std::string::npos);
}

/**
 * [Unit][App] serializeLinkStatsJson emits time bucket array entries.
 */
BOOST_AUTO_TEST_CASE(serialize_link_stats_json_time_buckets)
{
    LinkStatsView stats = makeSampleStats();
    stats.time_buckets.push_back({1700000000LL, 45});

    const auto json = serializeLinkStatsJson(stats);

    BOOST_CHECK(json.find("\"time_buckets\":[") != std::string::npos);
    BOOST_CHECK(json.find("\"bucket_start\":1700000000") != std::string::npos);
    BOOST_CHECK(json.find("\"count\":45") != std::string::npos);
}
