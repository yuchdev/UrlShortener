#define BOOST_TEST_MODULE SqliteCreateGetRoundtripTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(create_get_roundtrip)
{
    auto repo = MakeSqliteRepo("sqlite_create_get.sqlite3");
    BOOST_REQUIRE(repo->CreateLink(Req("abc123"), "id1", std::chrono::system_clock::time_point{}));
    auto got = repo->GetByShortCode("abc123");
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(got->id == "id1");
}
