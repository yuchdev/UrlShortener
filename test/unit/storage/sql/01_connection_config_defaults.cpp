#define BOOST_TEST_MODULE SqlConnectionConfigDefaultsTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"

BOOST_AUTO_TEST_CASE(defaults_are_sqlite_friendly)
{
    SqlConnectionConfig c;
    BOOST_TEST(c.backend == SqlBackendKind::sqlite);
    BOOST_TEST(c.auto_bootstrap);
    BOOST_TEST(c.busy_timeout.count() > 0);
}
