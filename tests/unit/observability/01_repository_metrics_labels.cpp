#define BOOST_TEST_MODULE 01_repository_metrics_labels
#include <boost/test/unit_test.hpp>
#include "url_shortener/observability/StorageObservability.hpp"

BOOST_AUTO_TEST_CASE(expected_labels_are_generated) {
    const auto labels = observability::repositoryMetricLabels("postgres", "create");
    BOOST_TEST(labels.at("backend") == "postgres");
    BOOST_TEST(labels.at("operation") == "create");
}

BOOST_AUTO_TEST_CASE(unknown_backend_does_not_break_labels) {
    const auto labels = observability::repositoryMetricLabels("customdb", "get");
    BOOST_TEST(labels.at("backend") == "unknown");
    BOOST_TEST(labels.at("operation") == "get");
}
