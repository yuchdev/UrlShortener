#define BOOST_TEST_MODULE CoreInterfaceSmokeCompileTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/IAnalyticsSink.hpp"
#include "url_shortener/storage/ICacheStore.hpp"
#include "url_shortener/storage/IMetadataRepository.hpp"
#include "url_shortener/storage/IRateLimiter.hpp"

class DummyRepo final : public IMetadataRepository {
public:
    bool CreateLink(const CreateLinkRequest&, const std::string&, const std::chrono::system_clock::time_point&, RepoError*) override { return true; }
    std::optional<LinkRecord> GetByShortCode(const std::string&, RepoError*) const override { return std::nullopt; }
    bool UpdateLink(const LinkRecord&, RepoError*) override { return true; }
    bool DeleteLink(const std::string&, RepoError*) override { return true; }
    std::vector<LinkRecord> ListLinks(const ListLinksQuery&, RepoError*) const override { return {}; }
    bool Exists(const std::string&, RepoError*) const override { return false; }
};

class DummyCache final : public ICacheStore {
public:
    std::optional<CacheValue> Get(const std::string&, CacheError*) override { return std::nullopt; }
    bool Set(const std::string&, const CacheValue&, std::optional<std::chrono::seconds>, CacheError*) override { return true; }
    bool Delete(const std::string&, CacheError*) override { return true; }
    bool ClearByPrefix(const std::string&, CacheError*) override { return true; }
};

class DummyAnalytics final : public IAnalyticsSink {
public:
    bool Emit(const LinkAccessEvent&, AnalyticsError*) override { return true; }
    bool Flush(AnalyticsError*) override { return true; }
};

class DummyRateLimiter final : public IRateLimiter {
public:
    RateLimitDecision Allow(const std::string&, uint64_t, std::chrono::seconds, RateLimitError*) override { return {}; }
};

BOOST_AUTO_TEST_CASE(interfaces_are_subclassable)
{
    DummyRepo repo;
    DummyCache cache;
    DummyAnalytics analytics;
    DummyRateLimiter limiter;
    BOOST_TEST(&repo != nullptr);
    BOOST_TEST(&cache != nullptr);
    BOOST_TEST(&analytics != nullptr);
    BOOST_TEST(&limiter != nullptr);
}
