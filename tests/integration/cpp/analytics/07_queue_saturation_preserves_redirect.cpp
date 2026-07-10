#define BOOST_TEST_MODULE QueueSaturationPreservesRedirect
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(skipped_until_inprocess_http_harness_available,
    *boost::unit_test::precondition([](boost::unit_test::test_unit_id) {
        boost::test_tools::assertion_result res(false);
        res.message() << "requires high-volume in-process redirect traffic harness for queue saturation assertions";
        return res;
    }))
{
}
