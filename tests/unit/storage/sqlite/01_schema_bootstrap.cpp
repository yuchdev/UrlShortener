#define BOOST_TEST_MODULE SqliteSchemaBootstrapTest
#include <boost/test/unit_test.hpp>
#include "sqlite_test_common.hpp"

BOOST_AUTO_TEST_CASE(schema_bootstrap_happens_on_first_use)
{
    auto repo = MakeSqliteRepo("sqlite_schema_bootstrap.sqlite3");
    BOOST_TEST(!repo->Exists("missing"));
}
