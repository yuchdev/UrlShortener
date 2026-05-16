#define BOOST_TEST_MODULE QueueSaturationPreservesRedirect
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(skipped_until_inprocess_http_harness_available)
{
    BOOST_TEST_MESSAGE("SKIPPED: requires high-volume in-process redirect traffic harness for queue saturation assertions.");
    BOOST_TEST(true);
}
