#define BOOST_TEST_MODULE MetadataContractDeleteRemovesRecordTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_delete_removes_record)
{
    for (auto& h : MakeMetadataHarnesses()) {
    BOOST_REQUIRE(h.repo->CreateLink(BasicRequest("abc123"), "id1", h.clock->now()));
    BOOST_REQUIRE(h.repo->DeleteLink("abc123"));
    BOOST_TEST(!h.repo->GetByShortCode("abc123").has_value());
}
}
