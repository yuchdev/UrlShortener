#define BOOST_TEST_MODULE AnalyticsPostgresRepositoryRoundtrip
#include <boost/test/unit_test.hpp>
#include <cstdlib>
#include "url_shortener/storage/sql/SqlClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(roundtrip_or_skip){
    const char* dsn = std::getenv("URL_SHORTENER_TEST_POSTGRES_DSN");
    if (!dsn || std::string(dsn).empty()) {
        BOOST_TEST_MESSAGE("SKIPPED: URL_SHORTENER_TEST_POSTGRES_DSN not set");
        BOOST_TEST(true);
        return;
    }
    storage::sql::SqlClickEventRepository repo(dsn);
    analytics::ClickEvent e;
    e.event_id = "pg-test-1";
    e.slug = "s";
    e.link_id = "l";
    e.domain = "d";
    e.status_code = 302;
    e.occurred_at = std::chrono::system_clock::time_point{};
    BOOST_TEST(repo.InsertBatch({e}, nullptr));
    analytics::AggregateQuery q;
    q.slug = std::string{"s"};
    q.from = std::chrono::system_clock::time_point{};
    q.to = std::chrono::system_clock::time_point{std::chrono::seconds(10)};
    analytics::AggregateStats s;
    BOOST_TEST(repo.GetAggregateStats(q, &s, nullptr));
    BOOST_TEST(s.total_attempts == 1u);
    BOOST_TEST(repo.DeleteOlderThan(std::chrono::system_clock::time_point{std::chrono::seconds(1)}, nullptr));
}

