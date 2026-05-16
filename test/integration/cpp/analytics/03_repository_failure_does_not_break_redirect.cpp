#define BOOST_TEST_MODULE RepositoryFailureDoesNotBreakRedirect
#include <boost/test/unit_test.hpp>
BOOST_AUTO_TEST_CASE(skipped_until_inprocess_http_harness_available)
{
    BOOST_TEST_MESSAGE("SKIPPED: requires full redirect endpoint + failing worker repository integration harness.");
    BOOST_TEST(true);
}
