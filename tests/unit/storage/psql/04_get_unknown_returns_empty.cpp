#define BOOST_TEST_MODULE PostgresGetUnknownReturnsEmptyTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(get_unknown_returns_empty)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    RepoError err = RepoError::none;
    auto out = repo.GetByShortCode("missing", &err);
    BOOST_TEST(!out.has_value());
    BOOST_TEST(err == RepoError::not_found);
}
