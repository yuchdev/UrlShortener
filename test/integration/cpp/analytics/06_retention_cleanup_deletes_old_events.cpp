#define BOOST_TEST_MODULE RetentionCleanupDeletesOldEvents
#include <boost/test/unit_test.hpp>

#include "url_shortener/analytics/RetentionCleanupJob.hpp"
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"

using namespace url_shortener::analytics;
using namespace url_shortener::storage::memory;

BOOST_AUTO_TEST_CASE(retention_deletes_only_old_events)
{
    InMemoryClickEventRepository repo;
    const auto now = std::chrono::system_clock::now();

    ClickEvent old_e{}; old_e.slug="s"; old_e.domain="d"; old_e.client_id_hash="h"; old_e.final_status_code=302; old_e.occurred_at = now - std::chrono::hours(24*10);
    ClickEvent new_e{}; new_e.slug="s"; new_e.domain="d"; new_e.client_id_hash="h"; new_e.final_status_code=302; new_e.occurred_at = now - std::chrono::hours(12);
    AnalyticsError err;
    BOOST_TEST(repo.InsertBatch({old_e, new_e}, &err));

    AnalyticsConfig cfg; cfg.retention.retention_days = 1;
    NullAnalyticsMetrics metrics;
    RetentionCleanupJob job(cfg, repo, metrics, [now]{ return now; });
    auto result = job.Run();
    BOOST_TEST(result.enabled);

    AggregateQuery q; q.slug = "s"; q.from = now - std::chrono::hours(24*30); q.to = now;
    AggregateStats stats;
    BOOST_TEST(repo.GetAggregateStats(q, &stats, &err));
    BOOST_TEST(stats.total_clicks == 1u);
}
