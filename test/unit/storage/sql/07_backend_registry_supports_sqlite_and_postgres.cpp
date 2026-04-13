#define BOOST_TEST_MODULE SqlBackendRegistrySupportsSqliteAndPostgresTest
#include <boost/test/unit_test.hpp>

#include <sstream>

#include "url_shortener/storage/sql/SqlBackendKind.hpp"

BOOST_AUTO_TEST_CASE(streaming_backend_kind_supports_sqlite_and_postgres)
{
    std::ostringstream oss;
    oss << SqlBackendKind::sqlite << "," << SqlBackendKind::postgres;
    BOOST_TEST(oss.str() == "sqlite,postgres");
}
