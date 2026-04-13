#define BOOST_TEST_MODULE MetadataContractUpdateExistingTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_update_existing)
{
    for (auto& h : MakeMetadataHarnesses()) {
    BOOST_REQUIRE(h.repo->CreateLink(BasicRequest("abc123", "https://a"), "id1", h.clock->now()));
    auto rec = h.repo->GetByShortCode("abc123");
    BOOST_REQUIRE(rec.has_value());
    rec->target_url = "https://b";
    BOOST_REQUIRE(h.repo->UpdateLink(*rec));
    BOOST_TEST(h.repo->GetByShortCode("abc123")->target_url == "https://b");
}
}
