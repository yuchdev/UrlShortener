#define BOOST_TEST_MODULE MetadataContractExpirySemanticsTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_expiry_semantics)
{
    for (auto& h : MakeMetadataHarnesses()) {
    auto req = BasicRequest("abc123");
    req.expires_at = h.clock->now() - std::chrono::seconds(1);
    BOOST_REQUIRE(h.repo->CreateLink(req, "id1", h.clock->now()));
    auto rec = h.repo->GetByShortCode("abc123");
    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST(rec->expires_at.has_value());
}
}
