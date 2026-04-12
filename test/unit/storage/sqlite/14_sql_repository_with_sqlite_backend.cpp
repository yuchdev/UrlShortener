#define BOOST_TEST_MODULE SqlRepositoryWithSqliteBackendTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(sql_repository_works_with_sqlite_components)
{
    auto repo = MakeSqliteRepo("sqlite_repo_backend.sqlite3");
    BOOST_REQUIRE(repo->CreateLink(Req("abc123"), "id1", std::chrono::system_clock::time_point{}));
    BOOST_TEST(repo->Exists("abc123"));
}
