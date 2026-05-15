#define BOOST_TEST_MODULE AnalyticsWorkerBatchFlush
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AnalyticsWorker.hpp"

using namespace url_shortener::analytics;

class Repo final : public IClickEventRepository {
public:
    std::vector<ClickEvent> captured;
    bool InsertBatch(const std::vector<ClickEvent>& events, AnalyticsError*) override { captured.insert(captured.end(), events.begin(), events.end()); return true; }
    bool GetAggregateStats(const AggregateQuery&, AggregateStats*, AnalyticsError*) override { return true; }
    bool DeleteOlderThan(Timestamp, AnalyticsError*) override { return true; }
};
class Metrics final : public IAnalyticsMetrics { public: std::uint64_t persisted=0; void OnEnqueued() noexcept override {} void OnDropped() noexcept override {} void OnPersisted(std::uint64_t c) noexcept override { persisted+=c; } void OnWorkerFailure() noexcept override {} void SetQueueDepth(std::uint64_t) noexcept override {} void ObserveEnqueueLatencyUs(std::uint64_t) noexcept override {} };

BOOST_AUTO_TEST_CASE(flush_batch_size)
{
    AnalyticsConfig cfg; cfg.enabled=true; cfg.batch_size=2; cfg.flush_interval=std::chrono::milliseconds(0);
    BoundedClickEventQueue q(10); Repo r; Metrics m; AnalyticsWorker w(cfg,q,r,m);
    ClickEvent a; a.slug="a"; a.domain="d"; a.status_code=302; ClickEvent b=a; b.slug="b";
    q.TryEnqueue(a); q.TryEnqueue(b);
    w.FlushOnce();
    BOOST_TEST(r.captured.size()==2);
    BOOST_TEST(m.persisted==2);
}
