#define BOOST_TEST_MODULE InMemoryCacheAsideIntegrationTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/LinkService.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(cache_aside_second_resolve_prefers_cache)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryMetadataRepository repo;
    InMemoryCacheStore cache(clock);
    InMemoryAnalyticsSink analytics;
    repo.CreateLink({"abc123", "https://a"}, "id1", clock.now());
    LinkService svc(repo, cache, analytics, clock);
    (void)svc.Resolve("abc123");
    auto second = svc.Resolve("abc123");
    BOOST_TEST(second.cache_hit);
}
