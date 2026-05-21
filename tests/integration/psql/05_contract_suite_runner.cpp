#define BOOST_TEST_MODULE PostgresContractSuiteRunnerSmoke
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(postgres_contract_runner_links_and_loads)
{
    PostgresSqlDialect d;
    BOOST_TEST(!d.InsertSql().empty());
}
