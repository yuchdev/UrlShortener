#define BOOST_TEST_MODULE CoreErrorTaxonomyTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/errors/AnalyticsError.hpp"
#include "url_shortener/storage/errors/CacheError.hpp"
#include "url_shortener/storage/errors/RateLimitError.hpp"
#include "url_shortener/storage/errors/RepoError.hpp"

BOOST_AUTO_TEST_CASE(error_enums_are_constructible_and_comparable)
{
    BOOST_TEST(RepoError::already_exists == RepoError::already_exists);
    BOOST_TEST(CacheError::unavailable != CacheError::none);
    BOOST_TEST(AnalyticsError::queue_full != AnalyticsError::none);
    BOOST_TEST(RateLimitError::invalid_key != RateLimitError::none);
}
