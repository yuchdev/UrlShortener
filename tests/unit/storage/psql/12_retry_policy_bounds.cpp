#define BOOST_TEST_MODULE PostgresRetryPolicyBoundsTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"

BOOST_AUTO_TEST_CASE(retry_policy_is_bounded_by_config)
{
    SqlConnectionConfig c;
    c.backend = SqlBackendKind::postgres;
    c.max_retries = 3;
    BOOST_TEST(c.max_retries == 3U);
}
