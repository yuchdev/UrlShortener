#define BOOST_TEST_MODULE MetadataContractGetUnknownTest
#include <boost/test/unit_test.hpp>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_get_unknown)
{
    auto h = MakeMetadataHarness();
    BOOST_TEST(!h.repo->GetByShortCode("missing").has_value());
}
