#define BOOST_TEST_MODULE cfg
#include <boost/test/unit_test.hpp>
#include "url_shortener/config/StorageConfig.hpp"
#include "url_shortener/composition/StorageFactory.hpp"
#include "url_shortener/core/clock.hpp"
BOOST_AUTO_TEST_CASE(case_test){SystemClock clock; auto a=BuildStorageAdapters(ParseStorageConfigYaml("metadata:
  backend: inmemory
"),clock); auto b=BuildStorageAdapters(ParseStorageConfigYaml("metadata:
  backend: sqlite
  sqlite_path: /tmp/y.db
"),clock); BOOST_TEST(a.metadata != nullptr); BOOST_TEST(b.metadata != nullptr);}
