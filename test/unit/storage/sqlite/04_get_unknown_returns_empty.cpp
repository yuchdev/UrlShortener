#define BOOST_TEST_MODULE SqliteGetUnknownReturnsEmptyTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(get_unknown_returns_empty)
{
    auto repo = MakeSqliteRepo("sqlite_unknown.sqlite3");
    RepoError err = RepoError::none;
    auto got = repo->GetByShortCode("missing", &err);
    BOOST_TEST(!got.has_value());
    BOOST_TEST(err == RepoError::not_found);
}
