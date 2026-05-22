#define BOOST_TEST_MODULE StatsEndpointReturnsAggregates
#include <boost/test/unit_test.hpp>
#include "url_shortener/http/AnalyticsStatsHandler.hpp"
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(handler_aggregate_smoke){ storage::memory::InMemoryClickEventRepository repo; analytics::ClickEvent a; a.slug="s"; a.link_id="l"; a.domain="sho.rt"; a.status_code=302; a.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(10)}; analytics::ClickEvent b=a; b.status_code=404; repo.InsertBatch({a,b},nullptr);
 http::AnalyticsStatsHandler h(repo); std::string body; int sc=0; h.Handle("s","0","1000","day",&body,&sc,"r"); BOOST_TEST(sc==200); BOOST_TEST(body.find("\"302\":1")!=std::string::npos); BOOST_TEST(body.find("\"404\":1")!=std::string::npos);
}
