#define BOOST_TEST_MODULE ClickEventBuilderRequiredFields
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/ClickEventBuilder.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(builder_populates_required_fields){RedirectEventContext c; c.slug="slug"; c.link_id="id"; c.domain="example.com"; c.final_status_code=404; c.client_network_identifier="1.2.3.4"; ClickEvent e; AnalyticsError err; ClientIdHashConfig hc{"salt",false,64}; BOOST_TEST(ClickEventBuilder::Build(c, {}, hc, &e, &err)); BOOST_TEST(!e.event_id.empty()); BOOST_TEST(!e.client_id_hash.empty()); BOOST_TEST(e.referrer.empty()); BOOST_TEST(e.user_agent.empty());}
