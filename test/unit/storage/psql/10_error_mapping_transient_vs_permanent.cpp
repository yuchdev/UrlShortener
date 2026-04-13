#define BOOST_TEST_MODULE PostgresErrorMapperTest
#include <boost/test/unit_test.hpp>

#include <stdexcept>

#include "url_shortener/storage/postgres/PostgresErrorMapper.hpp"

BOOST_AUTO_TEST_CASE(maps_transient_and_permanent)
{
    PostgresErrorMapper m;
    BOOST_TEST(m.MapException(std::runtime_error("sqlstate 40001 serialization")) == RepoError::transient_failure);
    BOOST_TEST(m.MapException(std::runtime_error("sqlstate 28P01 auth failed")) == RepoError::permanent_failure);
    BOOST_TEST(m.MapException(std::runtime_error("sqlstate 57014 statement timeout")) == RepoError::timeout);
}
