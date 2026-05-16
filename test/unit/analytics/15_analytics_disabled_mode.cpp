#define BOOST_TEST_MODULE AnalyticsDisabledMode
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AnalyticsService.hpp"
#include "url_shortener/analytics/AnalyticsWorker.hpp"

using namespace url_shortener::analytics;

namespace {
struct StubQueue final : IClickEventQueue {
    EnqueueResult TryEnqueue(ClickEvent) noexcept override { return EnqueueResult::Enqueued; }
    bool TryDequeue(ClickEvent&) noexcept override { return false; }
    QueueStats GetStats() const noexcept override { return {}; }
};
struct Repo final : IClickEventRepository {
    bool InsertBatch(const std::vector<ClickEvent>&, AnalyticsError*) override { return true; }
    bool GetAggregateStats(const AggregateQuery&, AggregateStats*, AnalyticsError*) override { return true; }
    bool DeleteOlderThan(Timestamp, AnalyticsError*) override { return true; }
};
}

BOOST_AUTO_TEST_CASE(disabled_service_is_noop_and_worker_does_not_start)
{
    AnalyticsConfig cfg; cfg.enabled = false;
    StubQueue q; NullAnalyticsMetrics m; Repo r;
    AnalyticsService svc(cfg, q, m);
    RedirectEventContext c; c.slug = "x"; c.domain = "d"; c.final_status_code = 302; c.client_network_identifier = "n";
    BOOST_CHECK_NO_THROW(svc.RecordRedirectAttempt(c));

    BoundedClickEventQueue worker_queue(8);
    AnalyticsWorker worker(cfg, worker_queue, r, m);
    worker.Start();
    BOOST_TEST(worker.State() == AnalyticsWorkerState::Stopped);
}
