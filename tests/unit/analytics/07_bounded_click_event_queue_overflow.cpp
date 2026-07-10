#define BOOST_TEST_MODULE BoundedClickEventQueueOverflow
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/BoundedClickEventQueue.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(overflow_drops){BoundedClickEventQueue q(1); ClickEvent e; e.slug="s"; e.domain="d"; e.status_code=302; BOOST_TEST(q.TryEnqueue(e)==EnqueueResult::Enqueued); BOOST_TEST(q.TryEnqueue(e)==EnqueueResult::DroppedFull); BOOST_TEST(q.GetStats().dropped_total==1);} 
