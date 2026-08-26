#define BOOST_TEST_MODULE SqliteServiceFullFlowTest
#include <boost/test/unit_test.hpp>

#include <filesystem>

#include "url_shortener/core/link_service.hpp"
#include "url_shortener/core/clock.hpp"
#include "url_shortener/storage/inmemory/in_memory_analytics_sink.hpp"
#include "url_shortener/storage/inmemory/in_memory_cache_store.hpp"
#include "url_shortener/storage/sql/sql_connection_config.hpp"
#include "url_shortener/storage/sql/sql_metadata_repository.hpp"
#include "url_shortener/storage/sql/sql_metadata_row_mapper.hpp"
#include "url_shortener/storage/sqlite/sqlite_session_factory.hpp"
#include "url_shortener/storage/sqlite/sqlite_sql_dialect.hpp"

BOOST_AUTO_TEST_CASE(service_resolves_via_sqlite_sql_repository)
{
    auto db = std::filesystem::temp_directory_path() / "07_sqlite_service_full_flow.db";
    std::filesystem::remove(db);

    SqlConnectionConfig cfg;
    cfg.backend = SqlBackendKind::sqlite;
    cfg.dsn = db.string();

    SqlMetadataRepository repo(
        std::make_shared<SqliteSessionFactory>(cfg),
        std::make_shared<SqliteSqlDialect>(),
        std::make_shared<SqlMetadataRowMapper>());
    SystemClock clock;
    InMemoryCacheStore cache(clock);
    InMemoryAnalyticsSink analytics;

    CreateLinkRequest req{"abc123", "https://example.com"};
    BOOST_REQUIRE(repo.CreateLink(req, "id1", std::chrono::system_clock::time_point{}));

    LinkService svc(repo, cache, analytics, clock);
    auto result = svc.Resolve("abc123");
    BOOST_TEST(result.status == ResolveStatus::redirect);
    BOOST_REQUIRE(result.target_url.has_value());
    BOOST_TEST(*result.target_url == "https://example.com");

    std::filesystem::remove(db);
}
