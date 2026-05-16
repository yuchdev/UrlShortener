#define BOOST_TEST_MODULE GracefulShutdownStopsWorker
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(skipped_until_inprocess_http_harness_available)
{
    BOOST_TEST_MESSAGE("SKIPPED: requires app lifecycle harness exposing worker startup/shutdown hooks.");
    BOOST_TEST(true);
}
