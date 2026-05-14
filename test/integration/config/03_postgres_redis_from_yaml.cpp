#define BOOST_TEST_MODULE cfg
#include <boost/test/unit_test.hpp>
#include "url_shortener/config/StorageConfig.hpp"
BOOST_AUTO_TEST_CASE(case_test){ auto c = ParseStorageConfigYaml(R"(metadata:
  backend: postgres
  postgres_dsn: host=localhost
cache:
  backend: redis
  redis_address: 127.0.0.1:6379
)"); BOOST_TEST(!c.metadata.backend.empty()); }
