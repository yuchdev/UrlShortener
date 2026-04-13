#define BOOST_TEST_MODULE PostgresDsnAndConnectionOptionsTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"

BOOST_AUTO_TEST_CASE(connection_config_contains_postgres_options)
{
    SqlConnectionConfig c;
    c.backend = SqlBackendKind::postgres;
    c.dsn = "host=localhost dbname=test";
    c.connect_timeout = std::chrono::milliseconds(1200);
    c.statement_timeout = std::chrono::milliseconds(3400);
    c.application_name = "url_shortener_tests";
    c.max_retries = 2;

    BOOST_TEST(c.backend == SqlBackendKind::postgres);
    BOOST_TEST(c.connect_timeout.count() == 1200);
    BOOST_TEST(c.statement_timeout.count() == 3400);
    BOOST_TEST(c.application_name.value() == "url_shortener_tests");
    BOOST_TEST(c.max_retries == 2U);
}
