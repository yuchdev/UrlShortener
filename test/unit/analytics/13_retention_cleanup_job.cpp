#define BOOST_TEST_MODULE RetentionCleanupJob
#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/RetentionCleanupJob.hpp"

using namespace url_shortener::analytics;

namespace {
struct FakeRepo final : IClickEventRepository {
    int calls = 0; bool fail = false; Timestamp cutoff{};
    bool InsertBatch(const std::vector<ClickEvent>&, AnalyticsError*) override { return true; }
    bool GetAggregateStats(const AggregateQuery&, AggregateStats*, AnalyticsError*) override { return true; }
    bool DeleteOlderThan(Timestamp c, AnalyticsError* error) override { ++calls; cutoff = c; if (fail) { if (error) { error->code = AnalyticsErrorCode::retention; error->message = "fail"; } return false; } return true; }
};
}

BOOST_AUTO_TEST_CASE(disabled_retention_does_not_call_repository)
{
    AnalyticsConfig cfg; cfg.retention.retention_days = 0;
    FakeRepo repo; NullAnalyticsMetrics metrics;
    RetentionCleanupJob job(cfg, repo, metrics, [] { return Timestamp{}; });
    auto r = job.Run();
    BOOST_TEST(!r.enabled);
    BOOST_TEST(repo.calls == 0);
}

BOOST_AUTO_TEST_CASE(enabled_retention_computes_cutoff)
{
    AnalyticsConfig cfg; cfg.retention.retention_days = 2;
    FakeRepo repo; NullAnalyticsMetrics metrics;
    const auto now = Timestamp{} + std::chrono::hours(100);
    RetentionCleanupJob job(cfg, repo, metrics, [now] { return now; });
    auto r = job.Run();
    BOOST_TEST(r.enabled);
    BOOST_TEST(repo.calls == 1);
    BOOST_TEST((now - repo.cutoff) == std::chrono::hours(48));
}

BOOST_AUTO_TEST_CASE(repository_failure_isolated)
{
    AnalyticsConfig cfg; cfg.retention.retention_days = 1;
    FakeRepo repo; repo.fail = true; NullAnalyticsMetrics metrics;
    RetentionCleanupJob job(cfg, repo, metrics);
    auto r = job.Run();
    BOOST_TEST(r.enabled);
    BOOST_TEST(static_cast<bool>(r.error));
}
