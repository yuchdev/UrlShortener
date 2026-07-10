#define BOOST_TEST_MODULE AnalyticsSqliteRepositoryRoundtrip
#include <boost/test/unit_test.hpp>
#include <filesystem>
#include "url_shortener/storage/sql/SqlClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(roundtrip_smoke){
    // Use a temporary file to verify that data persists to disk (not in-memory).
    const auto db_path = std::filesystem::temp_directory_path() / "analytics_sqlite_roundtrip_test.db";
    std::filesystem::remove(db_path);
    {
        storage::sql::SqlClickEventRepository repo(db_path.string());
        analytics::ClickEvent e;
        e.event_id = "test-evt-1";
        e.slug = "s";
        e.link_id = "l";
        e.domain = "d";
        e.status_code = 302;
        e.occurred_at = std::chrono::system_clock::time_point{};
        BOOST_TEST(repo.InsertBatch({e}, nullptr));
    }
    // Re-open the same file to verify persistence.
    {
        storage::sql::SqlClickEventRepository repo(db_path.string());
        analytics::AggregateQuery q;
        q.slug = std::string{"s"};
        q.from = {};
        q.to = std::chrono::system_clock::time_point{std::chrono::seconds(10)};
        analytics::AggregateStats s;
        BOOST_TEST(repo.GetAggregateStats(q, &s, nullptr));
        BOOST_TEST(s.total_attempts == 1u);
        BOOST_TEST(repo.DeleteOlderThan(
            std::chrono::system_clock::time_point{std::chrono::seconds(1)}, nullptr));
    }
    std::filesystem::remove(db_path);
}

