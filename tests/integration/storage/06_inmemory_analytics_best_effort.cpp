#define BOOST_TEST_MODULE InMemoryAnalyticsBestEffortIntegrationTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/LinkService.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
class FailingAnalytics final : public IAnalyticsSink {
public:
    bool Emit(const LinkAccessEvent&, AnalyticsError* error) override { if (error) *error = AnalyticsError::unavailable; return false; }
    bool Flush(AnalyticsError* error) override { if (error) *error = AnalyticsError::unavailable; return false; }
};
BOOST_AUTO_TEST_CASE(analytics_failure_does_not_fail_redirect)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryMetadataRepository repo;
    InMemoryCacheStore cache(clock);
    FailingAnalytics analytics;
    repo.CreateLink({"abc123", "https://a"}, "id1", clock.now());
    LinkService svc(repo, cache, analytics, clock);
    auto result = svc.Resolve("abc123");
    BOOST_TEST(result.status == ResolveStatus::redirect);
}
