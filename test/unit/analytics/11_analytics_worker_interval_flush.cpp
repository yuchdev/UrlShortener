#define BOOST_TEST_MODULE AnalyticsWorkerIntervalFlush
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AnalyticsWorker.hpp"
using namespace url_shortener::analytics;
class Repo2 final : public IClickEventRepository { public: std::size_t calls=0; std::size_t total=0; bool InsertBatch(const std::vector<ClickEvent>& events, AnalyticsError*) override { ++calls; total += events.size(); return true; } bool GetAggregateStats(const AggregateQuery&, AggregateStats*, AnalyticsError*) override { return true; } bool DeleteOlderThan(Timestamp, AnalyticsError*) override { return true; } };
BOOST_AUTO_TEST_CASE(partial_batch_flushes_without_real_sleep)
{
    AnalyticsConfig cfg; cfg.enabled=true; cfg.batch_size=5; cfg.flush_interval=std::chrono::milliseconds(0);
    BoundedClickEventQueue q(5); Repo2 r; NullAnalyticsMetrics m; AnalyticsWorker w(cfg,q,r,m);
    ClickEvent e; e.slug="s"; e.domain="d"; e.status_code=302; q.TryEnqueue(e);
    w.FlushOnce();
    BOOST_TEST(r.calls==1);
    BOOST_TEST(r.total==1);
}
