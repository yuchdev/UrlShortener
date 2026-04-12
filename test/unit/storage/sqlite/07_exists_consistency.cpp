#define BOOST_TEST_MODULE SqliteExistsConsistencyTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(exists_tracks_record_lifecycle)
{
    auto repo = MakeSqliteRepo("sqlite_exists.sqlite3");
    BOOST_TEST(!repo->Exists("abc123"));
    BOOST_REQUIRE(repo->CreateLink(Req("abc123"), "id1", std::chrono::system_clock::time_point{}));
    BOOST_TEST(repo->Exists("abc123"));
}
