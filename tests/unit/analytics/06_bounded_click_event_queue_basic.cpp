#define BOOST_TEST_MODULE BoundedClickEventQueueBasic
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/BoundedClickEventQueue.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(roundtrip){BoundedClickEventQueue q(2); ClickEvent a; a.slug="a"; a.domain="d"; a.status_code=302; BOOST_TEST(q.TryEnqueue(a)==EnqueueResult::Enqueued); ClickEvent out; BOOST_TEST(q.TryDequeue(out)); BOOST_TEST(out.slug=="a");}
BOOST_AUTO_TEST_CASE(capacity_zero_invalid){BOOST_CHECK_THROW(BoundedClickEventQueue(0), std::invalid_argument);} 
