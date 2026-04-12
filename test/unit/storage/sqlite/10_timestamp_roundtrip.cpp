#define BOOST_TEST_MODULE SqliteTimestampRoundtripTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(timestamp_roundtrip_epoch_seconds)
{
    auto repo = MakeSqliteRepo("sqlite_timestamps.sqlite3");
    auto now = std::chrono::system_clock::time_point(std::chrono::seconds(1234));
    BOOST_REQUIRE(repo->CreateLink(Req("abc123"), "id1", now));
    auto got = repo->GetByShortCode("abc123");
    BOOST_REQUIRE(got.has_value());
    BOOST_CHECK(got->created_at == now);
}
