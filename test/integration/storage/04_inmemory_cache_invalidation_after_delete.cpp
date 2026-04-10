#define BOOST_TEST_MODULE InMemoryCacheInvalidationAfterDeleteIntegrationTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/LinkService.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(delete_invalidates_cache)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryMetadataRepository repo;
    InMemoryCacheStore cache(clock);
    InMemoryAnalyticsSink analytics;
    repo.CreateLink({"abc123", "https://a"}, "id1", clock.now());
    LinkService svc(repo, cache, analytics, clock);
    (void)svc.Resolve("abc123");
    BOOST_REQUIRE(svc.Delete("abc123"));
    auto r = svc.Resolve("abc123");
    BOOST_TEST(r.status == ResolveStatus::not_found);
}
