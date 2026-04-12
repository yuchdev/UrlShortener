#define BOOST_TEST_MODULE SqliteOptionalFieldsRoundtripTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(optional_fields_roundtrip)
{
    auto repo = MakeSqliteRepo("sqlite_optionals.sqlite3");
    auto r = Req("abc123");
    r.owner = "owner";
    r.expires_at = std::chrono::system_clock::time_point(std::chrono::seconds(101));
    BOOST_REQUIRE(repo->CreateLink(r, "id1", std::chrono::system_clock::time_point{}));
    auto got = repo->GetByShortCode("abc123");
    BOOST_REQUIRE(got.has_value());
    BOOST_REQUIRE(got->owner.has_value());
    BOOST_TEST(*got->owner == "owner");
    BOOST_TEST(got->expires_at.has_value());
}
