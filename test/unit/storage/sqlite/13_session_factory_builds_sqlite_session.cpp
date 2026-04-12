#define BOOST_TEST_MODULE SqliteSessionFactoryBuildsSessionTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"

BOOST_AUTO_TEST_CASE(factory_builds_sqlite_session)
{
    SqlConnectionConfig c;
    c.backend = SqlBackendKind::sqlite;
    c.dsn = ":memory:";
    SqliteSessionFactory f(c);
    RepoError err = RepoError::none;
    auto s = f.Create(&err);
    BOOST_TEST(s != nullptr);
    BOOST_TEST(err == RepoError::none);
}
