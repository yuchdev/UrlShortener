#define BOOST_TEST_MODULE PostgresExistsConsistencyTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(exists_consistent_before_and_after_create)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    RepoError err = RepoError::none;
    BOOST_TEST(!repo.Exists("x", &err));
    CreateLinkRequest req{"x", "https://x"};
    BOOST_REQUIRE(repo.CreateLink(req, "id-x", std::chrono::system_clock::time_point{}, &err));
    BOOST_TEST(repo.Exists("x", &err));
}
