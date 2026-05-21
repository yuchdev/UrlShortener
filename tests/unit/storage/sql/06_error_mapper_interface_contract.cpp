#define BOOST_TEST_MODULE SqliteErrorMapperInterfaceContractTest
#include <boost/test/unit_test.hpp>
#include <stdexcept>
#include "url_shortener/storage/sqlite/SqliteErrorMapper.hpp"

BOOST_AUTO_TEST_CASE(error_mapper_maps_unique)
{
    SqliteErrorMapper m;
    std::runtime_error ex("UNIQUE constraint failed");
    BOOST_TEST(m.MapException(ex) == RepoError::already_exists);
}
