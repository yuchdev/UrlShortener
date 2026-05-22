#define BOOST_TEST_MODULE InMemoryClickEventRepositoryRetention
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(delete_older_than){ storage::memory::InMemoryClickEventRepository repo; analytics::ClickEvent e; e.slug="s"; e.link_id="l"; e.domain="d"; e.status_code=302;
 e.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(10)}; analytics::ClickEvent n=e; n.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(20)}; repo.InsertBatch({e,n},nullptr);
 repo.DeleteOlderThan(std::chrono::system_clock::time_point{std::chrono::seconds(20)},nullptr);
 analytics::AggregateQuery q; q.slug="s"; q.from={}; q.to=std::chrono::system_clock::time_point{std::chrono::seconds(100)}; analytics::AggregateStats s; repo.GetAggregateStats(q,&s,nullptr); BOOST_TEST(s.total_attempts==1);
}
