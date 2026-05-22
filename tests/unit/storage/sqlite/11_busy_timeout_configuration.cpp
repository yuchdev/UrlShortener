#define BOOST_TEST_MODULE SqliteBusyTimeoutConfigurationTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"

BOOST_AUTO_TEST_CASE(session_factory_applies_busy_timeout)
{
    SqlConnectionConfig c;
    c.backend = SqlBackendKind::sqlite;
    c.dsn = ":memory:";
    c.busy_timeout = std::chrono::milliseconds(777);
    SqliteSessionFactory f(c);
    RepoError err = RepoError::none;
    auto s = f.Create(&err);
    BOOST_REQUIRE(s);
    BOOST_TEST(err == RepoError::none);
}
