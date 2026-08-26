/**
 * @file 11_serialize_link_view_json_fields.cpp
 * @brief Unit test: serializeLinkViewJson emits all required JSON fields
 * correctly.
 */
#define BOOST_TEST_MODULE SerializeLinkViewJsonFields
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.h>

using namespace url_shortener;
using namespace url_shortener::app;

namespace
{

LinkView makeSampleView()
{
    LinkView v;
    v.id = "view-id-001";
    v.slug = "sample-slug";
    v.target_url = "https://sample.example.com/path";
    v.short_url = "http://sho.rt/sample-slug";
    v.created_at = "2026-01-01T00:00:00Z";
    v.updated_at = "2026-01-02T00:00:00Z";
    v.enabled = true;
    v.redirect_type = RedirectType::temporary;
    v.status = LinkStatus::active;
    return v;
}

}  // namespace

/**
 * [Unit][App] serializeLinkViewJson includes all required scalar fields.
 *
 * Scenario:
 *   Given a fully populated LinkView with known field values.
 *   When serializeLinkViewJson() is called.
 *   Then the returned string contains all required keys with correct values.
 *
 * API/Feature covered:
 *   - app::serializeLinkViewJson() completeness and correctness.
 *
 * If this breaks, first check:
 *   - Field names in serializeLinkViewJson ostringstream assembly.
 *   - jsonString() escaping for known-safe ASCII values.
 */
BOOST_AUTO_TEST_CASE(serialize_link_view_json_contains_required_fields)
{
    const auto view = makeSampleView();
    const auto json = serializeLinkViewJson(view);

    BOOST_CHECK(json.find("\"id\":\"view-id-001\"") != std::string::npos);
    BOOST_CHECK(json.find("\"slug\":\"sample-slug\"") != std::string::npos);
    BOOST_CHECK(json.find("\"url\":\"https://sample.example.com/path\"")
                != std::string::npos);
    BOOST_CHECK(json.find("\"short_url\":\"http://sho.rt/sample-slug\"")
                != std::string::npos);
    BOOST_CHECK(json.find("\"status\":\"active\"") != std::string::npos);
    BOOST_CHECK(json.find("\"redirect_type\":\"temporary\"")
                != std::string::npos);
    BOOST_CHECK(json.find("\"created_at\":\"2026-01-01T00:00:00Z\"")
                != std::string::npos);
    BOOST_CHECK(json.find("\"updated_at\":\"2026-01-02T00:00:00Z\"")
                != std::string::npos);
    BOOST_CHECK(json.find("\"tags\":[]") != std::string::npos);
    BOOST_CHECK(json.find("\"metadata\":{}") != std::string::npos);
    BOOST_CHECK(json.find("\"campaign\":null") != std::string::npos);
}

/**
 * [Unit][App] serializeLinkViewJson emits the stats sub-object with correct
 * counters.
 */
BOOST_AUTO_TEST_CASE(serialize_link_view_json_stats_subobject)
{
    LinkView view = makeSampleView();
    view.stats.total_redirects = 42;
    view.stats.redirects_24h = 5;
    view.stats.redirects_7d = 20;

    const auto json = serializeLinkViewJson(view);

    BOOST_CHECK(json.find("\"stats\":") != std::string::npos);
    BOOST_CHECK(json.find("\"total_redirects\":42") != std::string::npos);
    BOOST_CHECK(json.find("\"redirects_24h\":5") != std::string::npos);
    BOOST_CHECK(json.find("\"redirects_7d\":20") != std::string::npos);
    BOOST_CHECK(json.find("\"last_accessed_at\":null") != std::string::npos);
}

/**
 * [Unit][App] serializeLinkViewJson emits tags array when tags are present.
 */
BOOST_AUTO_TEST_CASE(serialize_link_view_json_tags_array)
{
    LinkView view = makeSampleView();
    view.tags = {"alpha", "beta"};

    const auto json = serializeLinkViewJson(view);

    BOOST_CHECK(json.find("\"tags\":[\"alpha\",\"beta\"]")
                != std::string::npos);
}
