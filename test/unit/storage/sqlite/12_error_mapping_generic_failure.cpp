#define BOOST_TEST_MODULE SqliteErrorMappingGenericFailureTest
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include "url_shortener/storage/sqlite/SqliteErrorMapper.hpp"

BOOST_AUTO_TEST_CASE(generic_failure_maps_to_permanent)
{
    SqliteErrorMapper m;
    std::runtime_error ex("something unexpected");
    BOOST_TEST(m.MapException(ex) == RepoError::permanent_failure);
}
