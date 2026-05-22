#define BOOST_TEST_MODULE StatsHandlerValidation
#include <boost/test/unit_test.hpp>
#include "url_shortener/http/AnalyticsStatsHandler.hpp"
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(validation_errors){ storage::memory::InMemoryClickEventRepository repo; http::AnalyticsStatsHandler h(repo); std::string b; int sc=0;
 h.Handle("","1","2","day",&b,&sc,"req"); BOOST_TEST(sc==404); BOOST_TEST(b.find("request_id")!=std::string::npos);
 h.Handle("s","x","2","day",&b,&sc,"req"); BOOST_TEST(sc==400);
 h.Handle("s","1","2","bad",&b,&sc,"req"); BOOST_TEST(sc==400);
}
