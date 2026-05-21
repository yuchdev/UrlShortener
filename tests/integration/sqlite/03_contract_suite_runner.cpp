#define BOOST_TEST_MODULE SqliteContractSuiteRunnerTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"

BOOST_AUTO_TEST_CASE(sqlite_contract_factory_smoke)
{
    auto harnesses = MakeMetadataHarnesses();
    BOOST_REQUIRE(!harnesses.empty());
    auto& h = harnesses[0];
    BOOST_REQUIRE(h.repo->CreateLink(BasicRequest("abc123"), "id1", h.clock->now()));
    BOOST_TEST(h.repo->Exists("abc123"));
}
