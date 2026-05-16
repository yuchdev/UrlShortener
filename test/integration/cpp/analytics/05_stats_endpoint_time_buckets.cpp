#define BOOST_TEST_MODULE StatsEndpointTimeBuckets
#include <boost/test/unit_test.hpp>
#include "url_shortener/http/AnalyticsStatsHandler.hpp"
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(handler_bucket_smoke){ storage::memory::InMemoryClickEventRepository repo; analytics::ClickEvent a; a.slug="s"; a.link_id="l"; a.domain="sho.rt"; a.status_code=302; a.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(0)}; analytics::ClickEvent b=a; b.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(3700)}; repo.InsertBatch({a,b},nullptr);
 http::AnalyticsStatsHandler h(repo); std::string body; int sc=0; h.Handle("s","0","10000","hour",&body,&sc,"r"); BOOST_TEST(sc==200); BOOST_TEST(body.find("time_buckets")!=std::string::npos);
}
