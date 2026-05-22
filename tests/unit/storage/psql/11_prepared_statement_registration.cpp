#define BOOST_TEST_MODULE PostgresPreparedStatementRegistrationTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(statement_catalog_like_queries_are_stable)
{
    PostgresSqlDialect d;
    BOOST_TEST(d.InsertSql().find(":short_code") != std::string::npos);
    BOOST_TEST(d.SelectByShortCodeSql().find(":short_code") != std::string::npos);
    BOOST_TEST(d.UpdateSql().find(":short_code") != std::string::npos);
}
