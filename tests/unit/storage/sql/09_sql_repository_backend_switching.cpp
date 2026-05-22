#define BOOST_TEST_MODULE SqlRepositoryBackendSwitchingTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"
#include "url_shortener/storage/sqlite/SqliteSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(dialects_expose_backend_specific_storage_semantics)
{
    SqliteSqlDialect sqlite;
    PostgresSqlDialect pg;
    BOOST_TEST(sqlite.BootstrapSchemaSql().find("attributes_json TEXT") != std::string::npos);
    BOOST_TEST(pg.BootstrapSchemaSql().find("attributes_json JSONB") != std::string::npos);
}
