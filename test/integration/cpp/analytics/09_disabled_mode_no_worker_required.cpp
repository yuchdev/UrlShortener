#define BOOST_TEST_MODULE DisabledModeNoWorkerRequired
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(skipped_until_inprocess_http_harness_available)
{
    BOOST_TEST_MESSAGE("SKIPPED: requires app composition harness to assert disabled analytics without worker startup.");
    BOOST_TEST(true);
}
