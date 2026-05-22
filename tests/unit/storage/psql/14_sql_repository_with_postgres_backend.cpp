#define BOOST_TEST_MODULE SqlRepositoryWithPostgresBackendTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(sql_repository_works_with_postgres_components)
{
    auto factory = std::make_shared<FakeFactory>();
    SqlMetadataRepository repo(factory, std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"pgrepo", "https://repo"};
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id-pgrepo", std::chrono::system_clock::time_point{}, &err));
    BOOST_TEST(repo.Exists("pgrepo", &err));
}
