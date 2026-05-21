#define BOOST_TEST_MODULE SqliteDuplicateMapsAlreadyExistsTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(duplicate_maps_already_exists)
{
    auto repo = MakeSqliteRepo("sqlite_dup.sqlite3");
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo->CreateLink(Req("abc123"), "id1", std::chrono::system_clock::time_point{}, &err));
    BOOST_TEST(!repo->CreateLink(Req("abc123"), "id2", std::chrono::system_clock::time_point{}, &err));
    BOOST_TEST(err == RepoError::already_exists);
}
