#define BOOST_TEST_MODULE cfg
#include <boost/test/unit_test.hpp>
#include "url_shortener/config/StorageConfig.hpp"
BOOST_AUTO_TEST_CASE(case_test){ auto c = ParseStorageConfigYaml(R"(metadata:
  backend: sqlite
  sqlite_path: /tmp/x.db
cache:
  backend: none
)"); BOOST_TEST(!c.metadata.backend.empty()); }
