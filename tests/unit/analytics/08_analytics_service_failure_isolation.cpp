#define BOOST_TEST_MODULE AnalyticsServiceFailureIsolation
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/AnalyticsService.hpp"

using namespace url_shortener::analytics;

class StubQueue final : public IClickEventQueue {
public:
    EnqueueResult result = EnqueueResult::Enqueued;
    QueueStats stats{};
    EnqueueResult TryEnqueue(ClickEvent) noexcept override { if (result == EnqueueResult::Enqueued) ++stats.enqueued_total; else ++stats.dropped_total; return result; }
    bool TryDequeue(ClickEvent&) noexcept override { return false; }
    QueueStats GetStats() const noexcept override { return stats; }
};
class CountingMetrics final : public IAnalyticsMetrics {
public: int enq=0, drop=0; void OnEnqueued() noexcept override {++enq;} void OnDropped() noexcept override {++drop;} void OnPersisted(std::uint64_t) noexcept override {} void OnWorkerFailure() noexcept override {} void SetQueueDepth(std::uint64_t) noexcept override {} void ObserveEnqueueLatencyUs(std::uint64_t) noexcept override {}
};

BOOST_AUTO_TEST_CASE(disabled_is_noop)
{
    AnalyticsConfig cfg; cfg.enabled = false;
    StubQueue q; CountingMetrics m; AnalyticsService svc(cfg, q, m);
    RedirectEventContext c; c.slug="s"; c.domain="d"; c.final_status_code=302; c.client_network_identifier="id";
    svc.RecordRedirectAttempt(c);
    BOOST_TEST(m.enq == 0);
    BOOST_TEST(m.drop == 0);
}

BOOST_AUTO_TEST_CASE(builder_failure_and_overflow_are_swallowed)
{
    AnalyticsConfig cfg; cfg.enabled = true; cfg.client_hash_salt = "salt";
    StubQueue q; CountingMetrics m; AnalyticsService svc(cfg, q, m);
    const auto now = std::chrono::system_clock::now();
    RedirectEventContext bad; bad.slug=""; bad.domain="d"; bad.final_status_code=302; bad.client_network_identifier="id"; bad.occurred_at = now;
    BOOST_CHECK_NO_THROW(svc.RecordRedirectAttempt(bad));
    q.result = EnqueueResult::DroppedFull;
    RedirectEventContext good; good.slug="s"; good.domain="d"; good.final_status_code=302; good.client_network_identifier="id"; good.occurred_at = now;
    BOOST_CHECK_NO_THROW(svc.RecordRedirectAttempt(good));
    BOOST_TEST(m.drop == 1);
}
