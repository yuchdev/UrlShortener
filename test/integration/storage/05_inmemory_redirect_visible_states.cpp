#define BOOST_TEST_MODULE InMemoryRedirectVisibleStatesIntegrationTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/LinkService.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(inactive_expired_unknown_states)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryMetadataRepository repo;
    InMemoryCacheStore cache(clock);
    InMemoryAnalyticsSink analytics;

    CreateLinkRequest inactive{"inactive1", "https://a"}; inactive.is_active = false;
    CreateLinkRequest expired{"expired1", "https://b"}; expired.expires_at = clock.now() - std::chrono::seconds(1);
    repo.CreateLink(inactive, "id1", clock.now());
    repo.CreateLink(expired, "id2", clock.now());

    LinkService svc(repo, cache, analytics, clock);
    BOOST_TEST(svc.Resolve("inactive1").status == ResolveStatus::inactive);
    BOOST_TEST(svc.Resolve("expired1").status == ResolveStatus::expired);
    BOOST_TEST(svc.Resolve("missing").status == ResolveStatus::not_found);
}
