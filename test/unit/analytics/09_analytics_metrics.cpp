#define BOOST_TEST_MODULE AnalyticsMetricsNull
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AnalyticsMetrics.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(null_metrics_safe)
{
    NullAnalyticsMetrics m;
    BOOST_CHECK_NO_THROW(m.OnEnqueued());
    BOOST_CHECK_NO_THROW(m.OnDropped());
    BOOST_CHECK_NO_THROW(m.OnPersisted(2));
    BOOST_CHECK_NO_THROW(m.OnWorkerFailure());
    BOOST_CHECK_NO_THROW(m.SetQueueDepth(3));
    BOOST_CHECK_NO_THROW(m.ObserveEnqueueLatencyUs(10));
}
BOOST_AUTO_TEST_CASE(metric_names_present)
{
    BOOST_TEST(std::string(AnalyticsMetricNames::events_enqueued_total) == "analytics_events_enqueued_total");
    BOOST_TEST(std::string(AnalyticsMetricNames::events_dropped_total) == "analytics_events_dropped_total");
}
