#define BOOST_TEST_MODULE MetadataContractListFilteringPaginationTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_list_filtering_pagination)
{
    for (auto& h : MakeMetadataHarnesses()) {
    auto a = BasicRequest("a11"); a.owner = "owner";
    auto b = BasicRequest("b22"); b.owner = "owner"; b.is_active = false;
    BOOST_REQUIRE(h.repo->CreateLink(a, "1", h.clock->now()));
    BOOST_REQUIRE(h.repo->CreateLink(b, "2", h.clock->now() + std::chrono::seconds(1)));
    ListLinksQuery q; q.owner = "owner"; q.include_inactive = false;
    BOOST_TEST(h.repo->ListLinks(q).size() == 1);
    q.include_inactive = true; q.limit = 1;
    BOOST_TEST(h.repo->ListLinks(q).size() == 1);
}
}
