#define BOOST_TEST_MODULE RedirectSuccessEmitsClickEvent
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(skipped_until_inprocess_http_harness_available,
    *boost::unit_test::precondition([](boost::unit_test::test_unit_id) {
        boost::test_tools::assertion_result res(false);
        res.message() << "in-process HTTP server harness for Stage 04 analytics integration is not yet wired in this repository";
        return res;
    }))
{
}
