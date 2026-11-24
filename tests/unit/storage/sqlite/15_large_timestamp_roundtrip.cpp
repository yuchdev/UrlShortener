#define BOOST_TEST_MODULE SqliteLargeTimestampRoundtripTest
#include <boost/test/unit_test.hpp>

#include "sqlite_test_common.hpp"

#include "url_shortener/storage/list_links_eval.hpp"
#include "url_shortener/storage/models/list_links_query.hpp"

namespace {

/// Epoch seconds for 2100-01-01T00:00:00Z — far beyond the 32-bit
/// signed INTEGER range (INT32_MAX = 2147483647, i.e. 2038-01-19).
constexpr long long kYear2100Epoch = 4102444800LL;

/// INT32_MAX + 1: the first value that overflows a signed 32-bit read.
constexpr long long kInt32OverflowEpoch = 2147483648LL;

std::chrono::system_clock::time_point EpochTp(long long seconds)
{
    return std::chrono::system_clock::time_point(std::chrono::seconds(seconds));
}

} // namespace

// Regression (Y2038): the SQLite metadata backend must round-trip epoch
// seconds larger than INT32_MAX. SOCI's SQLite backend derives a column's
// type from its declared type, so `created_at INTEGER` is fetched via
// sqlite3_column_int (32-bit) and silently truncated. This asserts exact
// equality through every read path.
BOOST_AUTO_TEST_CASE(large_timestamps_roundtrip_through_every_read_path)
{
    auto repo = MakeSqliteRepo("sqlite_large_timestamp_roundtrip.sqlite3");

    const auto created = EpochTp(kYear2100Epoch);
    const auto expires = EpochTp(kInt32OverflowEpoch);

    CreateLinkRequest req = Req("y2038a");
    req.expires_at = expires;
    BOOST_REQUIRE(repo->CreateLink(req, "id-y2038", created));

    // GetByShortCode
    auto by_code = repo->GetByShortCode("y2038a");
    BOOST_REQUIRE(by_code.has_value());
    BOOST_CHECK(by_code->created_at == created);
    BOOST_CHECK(by_code->updated_at == created);
    BOOST_REQUIRE(by_code->expires_at.has_value());
    BOOST_CHECK(*by_code->expires_at == expires);

    // GetById
    auto by_id = repo->GetById("id-y2038");
    BOOST_REQUIRE(by_id.has_value());
    BOOST_CHECK(by_id->created_at == created);
    BOOST_CHECK(by_id->updated_at == created);
    BOOST_REQUIRE(by_id->expires_at.has_value());
    BOOST_CHECK(*by_id->expires_at == expires);

    // ListLinks
    ListLinksQuery query;
    auto listed = repo->ListLinks(query);
    BOOST_REQUIRE_EQUAL(listed.size(), 1u);
    BOOST_CHECK(listed.front().created_at == created);
    BOOST_CHECK(listed.front().updated_at == created);
    BOOST_REQUIRE(listed.front().expires_at.has_value());
    BOOST_CHECK(*listed.front().expires_at == expires);

    // ListLinksPage
    LinkPage page;
    BOOST_REQUIRE(repo->ListLinksPage(query, &page));
    BOOST_REQUIRE_EQUAL(page.items.size(), 1u);
    BOOST_CHECK(page.items.front().created_at == created);
    BOOST_CHECK(page.items.front().updated_at == created);
    BOOST_REQUIRE(page.items.front().expires_at.has_value());
    BOOST_CHECK(*page.items.front().expires_at == expires);
}
