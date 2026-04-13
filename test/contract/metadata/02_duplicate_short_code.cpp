#define BOOST_TEST_MODULE MetadataContractDuplicateShortCodeTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_duplicate_short_code)
{
    for (auto& h : MakeMetadataHarnesses()) {
    RepoError err = RepoError::none;
    BOOST_REQUIRE(h.repo->CreateLink(BasicRequest("abc123"), "id1", h.clock->now(), &err));
    BOOST_TEST(!h.repo->CreateLink(BasicRequest("abc123"), "id2", h.clock->now(), &err));
    BOOST_TEST(err == RepoError::already_exists);
}
}
