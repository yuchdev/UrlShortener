#define BOOST_TEST_MODULE MetadataContractCreateGetRoundtripTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_create_get_roundtrip)
{
    for (auto& h : MakeMetadataHarnesses()) {
    BOOST_REQUIRE(h.repo->CreateLink(BasicRequest("abc123"), "id1", h.clock->now()));
    auto got = h.repo->GetByShortCode("abc123");
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(got->id == "id1");
}
}
