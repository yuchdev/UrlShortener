#define BOOST_TEST_MODULE SqlBackendKindSelectionTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"
#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"

BOOST_AUTO_TEST_CASE(postgres_backend_rejected_for_sqlite_factory)
{
    SqlConnectionConfig c;
    c.backend = SqlBackendKind::postgres;
    c.dsn = "dummy";
    SqliteSessionFactory f(c);
    RepoError err = RepoError::none;
    auto s = f.Create(&err);
    BOOST_TEST(!s);
    BOOST_TEST(err == RepoError::invalid_argument);
}
