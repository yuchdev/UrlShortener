#define BOOST_TEST_MODULE SqliteAttributesJsonRoundtripTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(attributes_roundtrip)
{
    auto repo = MakeSqliteRepo("sqlite_attrs.sqlite3");
    auto r = Req("abc123");
    r.attributes["a"] = "1";
    BOOST_REQUIRE(repo->CreateLink(r, "id1", std::chrono::system_clock::time_point{}));
    auto got = repo->GetByShortCode("abc123");
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(got->attributes.at("a") == "1");
}
