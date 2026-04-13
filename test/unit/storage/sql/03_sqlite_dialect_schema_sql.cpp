#define BOOST_TEST_MODULE SqliteDialectSchemaSqlTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sqlite/SqliteSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(schema_sql_contains_links_table)
{
    SqliteSqlDialect d;
    const auto sql = d.BootstrapSchemaSql();
    BOOST_TEST(sql.find("CREATE TABLE IF NOT EXISTS links") != std::string::npos);
}
