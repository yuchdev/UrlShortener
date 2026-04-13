#define BOOST_TEST_MODULE SqliteUpdateExistingTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(update_existing_record)
{
    auto repo = MakeSqliteRepo("sqlite_update.sqlite3");
    BOOST_REQUIRE(repo->CreateLink(Req("abc123", "https://a"), "id1", std::chrono::system_clock::time_point{}));
    auto rec = repo->GetByShortCode("abc123");
    BOOST_REQUIRE(rec.has_value());
    rec->target_url = "https://b";
    BOOST_REQUIRE(repo->UpdateLink(*rec));
    BOOST_TEST(repo->GetByShortCode("abc123")->target_url == "https://b");
}
