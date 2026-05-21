#define BOOST_TEST_MODULE ClientIdHasher
#include <boost/test/unit_test.hpp>
#include "url_shortener/analytics/ClientIdHasher.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(deterministic){ClientIdHashConfig c{"salt",false,64}; auto a=ClientIdHasher::Hash("1.2.3.4",c); auto b=ClientIdHasher::Hash("1.2.3.4",c); BOOST_TEST(a==b);}
BOOST_AUTO_TEST_CASE(salt_rotation_changes_hash){auto a=ClientIdHasher::Hash("1.2.3.4",{"salt1",false,64}); auto b=ClientIdHasher::Hash("1.2.3.4",{"salt2",false,64}); BOOST_TEST(a!=b);}
BOOST_AUTO_TEST_CASE(empty_salt_rejected){AnalyticsError e; auto h=ClientIdHasher::Hash("1.2.3.4",{"",false,64},&e); BOOST_TEST(h.empty()); BOOST_TEST(e.code==AnalyticsErrorCode::validation);}
BOOST_AUTO_TEST_CASE(raw_ip_never_returned){auto h=ClientIdHasher::Hash("1.2.3.4",{"salt",false,64}); BOOST_TEST(h!="1.2.3.4");}
