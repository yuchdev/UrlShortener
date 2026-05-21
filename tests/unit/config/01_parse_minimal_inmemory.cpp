#define BOOST_TEST_MODULE 01_parse_minimal_inmemory
#include <boost/test/unit_test.hpp>
#include "url_shortener/config/StorageConfig.hpp"
#include "url_shortener/composition/StorageFactory.hpp"
#include "url_shortener/core/clock.hpp"
BOOST_AUTO_TEST_CASE(case_test){
    SystemClock clock;
    auto c = ParseStorageConfigYaml("metadata:
  backend: inmemory
");
    if (std::string("01_parse_minimal_inmemory")=="05_invalid_backend_name") BOOST_CHECK_THROW(ParseStorageConfigYaml("metadata:
  backend: bad
"), std::runtime_error);
    else if (std::string("01_parse_minimal_inmemory")=="06_missing_required_fields") BOOST_CHECK_THROW(ParseStorageConfigYaml("metadata:
  backend: sqlite
"), std::runtime_error);
    else if (std::string("01_parse_minimal_inmemory")=="07_default_values") { BOOST_TEST(c.cache.backend=="none"); BOOST_TEST(c.analytics.backend=="noop"); }
    else { auto a = BuildStorageAdapters(c, clock); BOOST_TEST(a.metadata != nullptr); }
}
