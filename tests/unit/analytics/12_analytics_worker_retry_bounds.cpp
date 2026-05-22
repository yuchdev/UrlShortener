#define BOOST_TEST_MODULE AnalyticsWorkerRetryBounds
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AnalyticsWorker.hpp"
using namespace url_shortener::analytics;
class FlakyRepo final : public IClickEventRepository { public: int fail_count=0; int calls=0; bool InsertBatch(const std::vector<ClickEvent>&, AnalyticsError*) override { ++calls; if (calls<=fail_count) return false; return true; } bool GetAggregateStats(const AggregateQuery&, AggregateStats*, AnalyticsError*) override { return true; } bool DeleteOlderThan(Timestamp, AnalyticsError*) override { return true; } };
class M final : public IAnalyticsMetrics { public: int failures=0; int persisted=0; void OnEnqueued() noexcept override {} void OnDropped() noexcept override {} void OnPersisted(std::uint64_t c) noexcept override { persisted += (int)c; } void OnWorkerFailure() noexcept override { ++failures; } void SetQueueDepth(std::uint64_t) noexcept override {} void ObserveEnqueueLatencyUs(std::uint64_t) noexcept override {} };
BOOST_AUTO_TEST_CASE(retries_within_bounds_and_recovers)
{
    AnalyticsConfig cfg; cfg.enabled=true; cfg.batch_size=1; cfg.flush_interval=std::chrono::milliseconds(0); cfg.retry.max_retry_attempts=2; cfg.retry.retry_backoff=std::chrono::milliseconds(0);
    BoundedClickEventQueue q(10); FlakyRepo repo; M m; AnalyticsWorker w(cfg,q,repo,m);
    ClickEvent e; e.slug="s"; e.domain="d"; e.status_code=302;
    repo.fail_count=2; q.TryEnqueue(e); w.FlushOnce(); BOOST_TEST(repo.calls==3); BOOST_TEST(m.failures==0); BOOST_TEST(m.persisted==1);
    repo.calls=0; repo.fail_count=5; q.TryEnqueue(e); w.FlushOnce(); BOOST_TEST(repo.calls==3); BOOST_TEST(m.failures==1);
}
