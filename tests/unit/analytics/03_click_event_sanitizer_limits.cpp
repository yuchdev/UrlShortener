#define BOOST_TEST_MODULE ClickEventSanitizerLimits
#include <boost/test/unit_test.hpp>
#include <string>
#include "url_shortener/analytics/ClickEventSanitizer.hpp"
using namespace url_shortener::analytics;
BOOST_AUTO_TEST_CASE(limits_and_control_strip){SanitizationLimits l; std::string longv(700,'a'); BOOST_TEST(ClickEventSanitizer::SanitizeReferrer(longv,l).size()==512); BOOST_TEST(ClickEventSanitizer::SanitizeUserAgent(longv,l).size()==512); BOOST_TEST(ClickEventSanitizer::SanitizeDomain(longv,l).size()==255); BOOST_TEST(ClickEventSanitizer::SanitizeDomain(" \nexa\x01mple.com\t ",l)=="example.com");}
