/** @file 08_redirect_status_test.cpp @brief Unit test: redirect status selection from redirect type. */
#define BOOST_TEST_MODULE RedirectStatusTest
#include <boost/test/unit_test.hpp>
#include "../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(redirect_status_selection_from_redirect_type)
{
    Link temp;
    temp.redirect_type = RedirectType::temporary;
    BOOST_TEST(redirectStatusFor(temp) == http::status::found);

    Link perm;
    perm.redirect_type = RedirectType::permanent;
    BOOST_TEST(redirectStatusFor(perm) == http::status::moved_permanently);
}
