#define BOOST_TEST_MODULE InMemoryClickEventRepositoryBuckets
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(hour_day_buckets){ storage::memory::InMemoryClickEventRepository repo; analytics::ClickEvent e; e.slug="s"; e.link_id="l"; e.domain="d"; e.status_code=302;
 e.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(3599)}; analytics::ClickEvent e2=e; e2.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(3600)}; repo.InsertBatch({e,e2},nullptr);
 analytics::AggregateQuery q; q.slug="s"; q.from={}; q.to=std::chrono::system_clock::time_point{std::chrono::seconds(7200)}; q.bucket=analytics::AggregateBucket::hour; analytics::AggregateStats s; repo.GetAggregateStats(q,&s,nullptr); BOOST_TEST(s.time_buckets.size()==2);
}
