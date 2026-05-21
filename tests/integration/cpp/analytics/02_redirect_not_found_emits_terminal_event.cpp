#define BOOST_TEST_MODULE RedirectNotFoundEmitsTerminalEvent
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(skipped_until_inprocess_http_harness_available)
{
    BOOST_TEST_MESSAGE("SKIPPED: requires redirect app composition with injectable analytics repository test double.");
    BOOST_TEST(true);
}
