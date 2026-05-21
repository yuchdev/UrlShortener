#define BOOST_TEST_MODULE cfg
#include <boost/test/unit_test.hpp>
#include "url_shortener/config/StorageConfig.hpp"
BOOST_AUTO_TEST_CASE(case_test){ BOOST_CHECK_THROW(ParseStorageConfigYaml(R"(metadata:
  backend: postgres
)"), std::runtime_error); }
