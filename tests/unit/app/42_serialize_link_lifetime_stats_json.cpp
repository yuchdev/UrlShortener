/**
 * @file 42_serialize_link_lifetime_stats_json.cpp
 * @brief Unit test: serializeLinkLifetimeStatsJson field order, null/string
 * last_accessed_at, slug escaping, and counter boundaries.
 */
#define BOOST_TEST_MODULE SerializeLinkLifetimeStatsJson
#include <cstdint>
#include <string>

#include <boost/test/unit_test.hpp>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/utils.hpp>

using url_shortener::app::LinkView;
using url_shortener::app::serializeLinkLifetimeStatsJson;

namespace
{

// A LinkView carrying only the fields the lifetime-stats body reads; the rest
// (short_url, tags, metadata, ...) must never appear in the output.
LinkView makeView()
{
    LinkView view;
    view.slug = "my-slug";
    view.created_at = "2025-01-01T00:00:00Z";
    view.short_url = "http://sho.rt/my-slug";  // must not leak into the body
    view.tags = {"ignored"};                    // must not leak into the body
    return view;
}

}  // namespace

// Type: unit (byte-exact field order, C3).
// Scenario: a view with a null last_accessed_at serializes to the exact six-key
//   lifetime body in the pinned order, with last_accessed_at as JSON null and no
//   short_url/tags keys.
// Boundaries: full-string equality, not substring; the key order is load-bearing
//   for REST parity.
// On failure first check: did a field get added, reordered, or did
//   last_accessed_at stop emitting bare null?
BOOST_AUTO_TEST_CASE(null_last_accessed_exact_order)
{
    const std::string json = serializeLinkLifetimeStatsJson(makeView());

    BOOST_TEST(json
               == R"({"slug":"my-slug","total_redirects":0,"redirects_24h":0,)"
                  R"("redirects_7d":0,"last_accessed_at":null,)"
                  R"("created_at":"2025-01-01T00:00:00Z"})");
}

// Type: unit (string last_accessed_at, counters applied).
// Scenario: a view with a present last_accessed_at and non-zero counters emits
//   the timestamp as a quoted string and the counters as bare numbers.
// Boundaries: last_accessed_at is a JSON string here, not null; each counter is
//   taken from LinkView.stats in order.
// On failure first check: did the counters read from the wrong field, or did
//   last_accessed_at lose its quoting?
BOOST_AUTO_TEST_CASE(string_last_accessed_with_counters)
{
    LinkView view = makeView();
    view.stats.total_redirects = 7;
    view.stats.redirects_24h = 3;
    view.stats.redirects_7d = 5;
    view.stats.last_accessed_at = "2025-06-01T12:00:00Z";

    const std::string json = serializeLinkLifetimeStatsJson(view);

    BOOST_TEST(json
               == R"({"slug":"my-slug","total_redirects":7,"redirects_24h":3,)"
                  R"("redirects_7d":5,)"
                  R"("last_accessed_at":"2025-06-01T12:00:00Z",)"
                  R"("created_at":"2025-01-01T00:00:00Z"})");
}

// Type: unit (escaping via jsonString).
// Scenario: a slug containing a quote, backslash and a multi-byte UTF-8 code
//   point is escaped exactly as jsonString escapes it, in the slug position.
// Boundaries: the serializer delegates string escaping to jsonString; unicode
//   bytes pass through, quote/backslash are escaped.
// On failure first check: did the serializer stop routing slug through
//   jsonString, or hand-roll escaping differently?
BOOST_AUTO_TEST_CASE(slug_is_escaped_via_json_string)
{
    LinkView view = makeView();
    view.slug = "qu\"ote\\\xcf\x80";  // contains ", \, and U+03C0 (pi)

    const std::string json = serializeLinkLifetimeStatsJson(view);

    const std::string expected = "{\"slug\":"
                                 + url_shortener::jsonString(view.slug)
                                 + ",\"total_redirects\":0,\"redirects_24h\":0,"
                                   "\"redirects_7d\":0,"
                                   "\"last_accessed_at\":null,"
                                   "\"created_at\":\"2025-01-01T00:00:00Z\"}";
    BOOST_TEST(json == expected);
    // The escaped quote/backslash are present verbatim in the output.
    BOOST_TEST(json.find("qu\\\"ote\\\\") != std::string::npos);
}

// Type: unit (counter boundaries 0 and UINT64_MAX).
// Scenario: counters at their extreme values serialize as exact unsigned decimal
//   with no overflow or sign.
// Boundaries: UINT64_MAX (18446744073709551615) must print in full; a zero
//   counter prints as 0.
// On failure first check: did a counter get narrowed to a signed type or
//   truncated?
BOOST_AUTO_TEST_CASE(counters_at_zero_and_uint64_max)
{
    LinkView view = makeView();
    view.stats.total_redirects = UINT64_MAX;
    view.stats.redirects_24h = 0;
    view.stats.redirects_7d = UINT64_MAX;

    const std::string json = serializeLinkLifetimeStatsJson(view);

    const std::string max_str = std::to_string(UINT64_MAX);
    const std::string expected = "{\"slug\":\"my-slug\",\"total_redirects\":"
                                 + max_str + ",\"redirects_24h\":0,"
                                             "\"redirects_7d\":"
                                 + max_str
                                 + ",\"last_accessed_at\":null,"
                                   "\"created_at\":\"2025-01-01T00:00:00Z\"}";
    BOOST_TEST(json == expected);
}
