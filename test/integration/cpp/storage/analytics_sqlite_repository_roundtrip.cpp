#define BOOST_TEST_MODULE AnalyticsSqliteRepositoryRoundtrip
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sql/SqlClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(roundtrip_smoke){ storage::sql::SqlClickEventRepository repo; analytics::ClickEvent e; e.slug="s"; e.link_id="l"; e.domain="d"; e.status_code=302; e.occurred_at=std::chrono::system_clock::time_point{}; BOOST_TEST(repo.InsertBatch({e},nullptr)); analytics::AggregateQuery q; q.slug="s"; q.from={}; q.to=std::chrono::system_clock::time_point{std::chrono::seconds(10)}; analytics::AggregateStats s; BOOST_TEST(repo.GetAggregateStats(q,&s,nullptr)); BOOST_TEST(s.total_attempts==1); BOOST_TEST(repo.DeleteOlderThan(std::chrono::system_clock::time_point{std::chrono::seconds(1)},nullptr)); }
