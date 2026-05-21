#define BOOST_TEST_MODULE InMemoryCacheFailOpenBehaviorTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/core/LinkService.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
class FailingCache final : public ICacheStore {
public:
    std::optional<CacheValue> Get(const std::string&, CacheError* error) override { if (error) *error = CacheError::unavailable; return std::nullopt; }
    bool Set(const std::string&, const CacheValue&, std::optional<std::chrono::seconds>, CacheError* error) override { if (error) *error = CacheError::unavailable; return false; }
    bool Delete(const std::string&, CacheError*) override { return true; }
    bool ClearByPrefix(const std::string&, CacheError*) override { return true; }
};
BOOST_AUTO_TEST_CASE(cache_failure_does_not_break_resolution)
{
    ManualClock clock(std::chrono::system_clock::time_point{});
    InMemoryMetadataRepository repo;
    InMemoryAnalyticsSink analytics;
    FailingCache cache;
    repo.CreateLink({"abc123", "https://a"}, "1", clock.now());
    LinkService svc(repo, cache, analytics, clock);
    auto result = svc.Resolve("abc123");
    BOOST_TEST(result.status == ResolveStatus::redirect);
}
