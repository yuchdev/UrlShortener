#define BOOST_TEST_MODULE SqliteContractSuiteRunnerTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"

BOOST_AUTO_TEST_CASE(sqlite_contract_factory_smoke)
{
    auto h = MakeMetadataHarness();
    BOOST_REQUIRE(h.repo->CreateLink(BasicRequest("abc123"), "id1", h.clock->now()));
    BOOST_TEST(h.repo->Exists("abc123"));
}
