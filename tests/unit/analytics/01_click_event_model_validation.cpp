#define BOOST_TEST_MODULE ClickEventModelValidation
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/ClickEvent.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(valid_event){ClickEvent e; e.event_id="evt-1"; e.occurred_at=std::chrono::system_clock::now(); e.slug="s"; e.domain="d"; e.status_code=302; BOOST_TEST(e.Validate());}
BOOST_AUTO_TEST_CASE(empty_event_id_rejected){ClickEvent e; e.occurred_at=std::chrono::system_clock::now(); e.slug="s"; e.domain="d"; e.status_code=302; BOOST_TEST(!e.Validate());}
BOOST_AUTO_TEST_CASE(epoch_timestamp_rejected){ClickEvent e; e.event_id="evt-1"; e.slug="s"; e.domain="d"; e.status_code=302; BOOST_TEST(!e.Validate());}
BOOST_AUTO_TEST_CASE(empty_slug_rejected){ClickEvent e; e.event_id="evt-1"; e.occurred_at=std::chrono::system_clock::now(); e.domain="d"; e.status_code=302; BOOST_TEST(!e.Validate());}
BOOST_AUTO_TEST_CASE(empty_domain_rejected){ClickEvent e; e.event_id="evt-1"; e.occurred_at=std::chrono::system_clock::now(); e.slug="s"; e.status_code=302; BOOST_TEST(!e.Validate());}
BOOST_AUTO_TEST_CASE(invalid_status_rejected){ClickEvent e; e.event_id="evt-1"; e.occurred_at=std::chrono::system_clock::now(); e.slug="s"; e.domain="d"; e.status_code=200; BOOST_TEST(!e.Validate());}
