#define BOOST_TEST_MODULE StatsHandlerResponseMapping
#include <boost/test/unit_test.hpp>
#include "url_shortener/http/AnalyticsStatsHandler.hpp"
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(response_contains_fields){ storage::memory::InMemoryClickEventRepository repo; analytics::ClickEvent e; e.slug="s"; e.link_id="l"; e.domain="sho.rt"; e.status_code=302; e.occurred_at=std::chrono::system_clock::time_point{std::chrono::seconds(10)}; repo.InsertBatch({e},nullptr);
 http::AnalyticsStatsHandler h(repo); std::string b; int sc=0; h.Handle("s","0","100","day",&b,&sc,"r1"); BOOST_TEST(sc==200); BOOST_TEST(b.find("\"slug\":\"s\"")!=std::string::npos); BOOST_TEST(b.find("total_attempts")!=std::string::npos);
}
