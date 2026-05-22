#define BOOST_TEST_MODULE PostgresConfigRoundtripTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"

BOOST_AUTO_TEST_CASE(postgres_specific_config_roundtrip)
{
    SqlConnectionConfig c;
    c.backend = SqlBackendKind::postgres;
    c.dsn = "host=localhost dbname=url_shortener";
    c.connect_timeout = std::chrono::milliseconds(2000);
    c.statement_timeout = std::chrono::milliseconds(7000);
    c.application_name = "url_shortener";
    c.max_retries = 1;

    SqlConnectionConfig copy = c;
    BOOST_TEST(copy.backend == SqlBackendKind::postgres);
    BOOST_TEST(copy.dsn == c.dsn);
    BOOST_TEST(copy.connect_timeout.count() == 2000);
    BOOST_TEST(copy.statement_timeout.count() == 7000);
    BOOST_TEST(copy.application_name.value() == "url_shortener");
    BOOST_TEST(copy.max_retries == 1U);
}
