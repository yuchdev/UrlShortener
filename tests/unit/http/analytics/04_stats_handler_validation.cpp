#define BOOST_TEST_MODULE AnalyticsStatsHandlerValidation
#include <boost/test/unit_test.hpp>
#include "url_shortener/http/AnalyticsStatsHandler.hpp"
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"
using namespace url_shortener;
BOOST_AUTO_TEST_CASE(invalid_inputs_400){
 storage::memory::InMemoryClickEventRepository repo; http::AnalyticsStatsHandler h(repo); std::string body; int code=0;
 h.Handle("slug","bad","1","day",&body,&code,"r1"); BOOST_TEST(code==400);
 h.Handle("slug","1","2","bad",&body,&code,"r1"); BOOST_TEST(code==400);
}
