#define BOOST_TEST_MODULE AnalyticsConfigLoading
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AnalyticsConfig.hpp"
using namespace url_shortener::analytics;

BOOST_AUTO_TEST_CASE(defaults_are_valid)
{
    AnalyticsConfig cfg; cfg.client_hash_salt = "x";
    AnalyticsError err; BOOST_TEST(cfg.Validate(&err));
}

BOOST_AUTO_TEST_CASE(invalid_values_rejected)
{
    AnalyticsConfig cfg; cfg.client_hash_salt = "x"; cfg.queue_capacity = 0;
    BOOST_TEST(!cfg.Validate());
}

BOOST_AUTO_TEST_CASE(production_empty_salt_rejected)
{
    AnalyticsConfig cfg; cfg.production_mode = true; cfg.client_hash_salt.clear();
    BOOST_TEST(!cfg.Validate());
}
