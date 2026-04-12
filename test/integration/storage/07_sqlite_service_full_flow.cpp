#define BOOST_TEST_MODULE SqliteServiceFullFlowTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/core/LinkService.hpp"
#include "url_shortener/storage/inmemory/InMemoryAnalyticsSink.hpp"
#include "url_shortener/storage/inmemory/InMemoryCacheStore.hpp"
#include "url_shortener/storage/sql/SqlConnectionConfig.hpp"
#include "url_shortener/storage/sql/SqlMetadataRepository.hpp"
#include "url_shortener/storage/sql/SqlMetadataRowMapper.hpp"
#include "url_shortener/storage/sqlite/SqliteSessionFactory.hpp"
#include "url_shortener/storage/sqlite/SqliteSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(service_resolves_via_sqlite_sql_repository)
{
    SqlConnectionConfig cfg;
    cfg.backend = SqlBackendKind::sqlite;
    cfg.dsn = ":memory:";

    SqlMetadataRepository repo(
        std::make_shared<SqliteSessionFactory>(cfg),
        std::make_shared<SqliteSqlDialect>(),
        std::make_shared<SqlMetadataRowMapper>());
    InMemoryCacheStore cache;
    InMemoryAnalyticsSink analytics;
    SystemClock clock;

    CreateLinkRequest req{"abc123", "https://example.com"};
    BOOST_REQUIRE(repo.CreateLink(req, "id1", std::chrono::system_clock::time_point{}));

    LinkService svc(repo, cache, analytics, clock);
    auto result = svc.Resolve("abc123");
    BOOST_TEST(result.status == ResolveStatus::redirect);
    BOOST_REQUIRE(result.target_url.has_value());
    BOOST_TEST(*result.target_url == "https://example.com");
}
