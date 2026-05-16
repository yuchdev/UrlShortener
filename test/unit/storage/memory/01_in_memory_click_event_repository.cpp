#define BOOST_TEST_MODULE InMemoryClickEventRepositoryBasic
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(insert_aggregate_unknown_slug){
    storage::memory::InMemoryClickEventRepository repo;
    analytics::ClickEvent a; a.slug="abc"; a.link_id="id1"; a.domain="sho.rt"; a.status_code=302; a.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(10)};
    analytics::ClickEvent b=a; b.status_code=404;
    BOOST_TEST(repo.InsertBatch({a,b},nullptr));
    analytics::AggregateQuery q; q.slug="abc"; q.from={}; q.to=std::chrono::system_clock::time_point{std::chrono::seconds(100)};
    analytics::AggregateStats s; BOOST_TEST(repo.GetAggregateStats(q,&s,nullptr));
    BOOST_TEST(s.total_attempts==2); BOOST_TEST(s.successful_redirects==1);
    analytics::AggregateQuery q2=q; q2.slug="missing"; analytics::AggregateStats s2; repo.GetAggregateStats(q2,&s2,nullptr); BOOST_TEST(s2.total_attempts==0);
}
