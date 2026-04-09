#define BOOST_TEST_MODULE LinkManagementReservedSlugTest
#include <boost/test/unit_test.hpp>
#include "../../../src/url_shortener.cpp"

BOOST_AUTO_TEST_CASE(reserved_slug_case_insensitive_matching)
{
    BOOST_TEST(isReservedSlug("API"));
    BOOST_TEST(isReservedSlug("Stats"));
    BOOST_TEST(isReservedSlug("LoGiN"));
    BOOST_TEST(!isReservedSlug("promo-2026"));
}
