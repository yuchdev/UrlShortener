#define BOOST_TEST_MODULE SqliteDeleteExistingTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(delete_existing)
{
    auto repo = MakeSqliteRepo("sqlite_delete.sqlite3");
    BOOST_REQUIRE(repo->CreateLink(Req("abc123"), "id1", std::chrono::system_clock::time_point{}));
    BOOST_REQUIRE(repo->DeleteLink("abc123"));
    BOOST_TEST(!repo->GetByShortCode("abc123").has_value());
}
