#define BOOST_TEST_MODULE InMemoryClickEventRepository
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(aggregates_basic){
 storage::memory::InMemoryClickEventRepository repo; analytics::ClickEvent e; e.slug="s"; e.link_id="l"; e.domain="d"; e.status_code=302; e.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(100)};
 analytics::ClickEvent e2=e; e2.status_code=404;
 BOOST_TEST(repo.InsertBatch({e,e2},nullptr));
 analytics::AggregateQuery q; q.slug="s"; q.from=std::chrono::system_clock::time_point{}; q.to=std::chrono::system_clock::time_point{std::chrono::seconds(200)}; q.bucket=analytics::AggregateBucket::hour;
 analytics::AggregateStats s; BOOST_TEST(repo.GetAggregateStats(q,&s,nullptr));
 BOOST_TEST(s.total_attempts==2);
 BOOST_TEST(s.successful_redirects==1);
}
