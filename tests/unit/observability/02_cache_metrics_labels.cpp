#define BOOST_TEST_MODULE 02_cache_metrics_labels
#include <boost/test/unit_test.hpp>
#include "url_shortener/observability/StorageObservability.hpp"

BOOST_AUTO_TEST_CASE(cache_labels_are_generated) {
    const auto labels = observability::cacheMetricLabels("redis", "hit");
    BOOST_TEST(labels.at("backend") == "redis");
    BOOST_TEST(labels.at("operation") == "hit");
}

BOOST_AUTO_TEST_CASE(cache_labels_unknown_operation_is_normalized) {
    const auto labels = observability::cacheMetricLabels("redis", "");
    BOOST_TEST(labels.at("operation") == "unknown");
}
