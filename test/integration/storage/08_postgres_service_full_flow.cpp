#define BOOST_TEST_MODULE PostgresServiceFullFlowSmoke
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/postgres/PostgresErrorMapper.hpp"

BOOST_AUTO_TEST_CASE(postgres_error_mapper_is_available_for_service_flow)
{
    PostgresErrorMapper mapper;
    BOOST_TEST(mapper.MapException(std::runtime_error("23505 duplicate")) == RepoError::already_exists);
}
