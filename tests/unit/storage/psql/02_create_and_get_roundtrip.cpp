#define BOOST_TEST_MODULE PostgresCreateAndGetRoundtripTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(create_then_get_uses_shared_sql_repository_contract)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"abc", "https://example.com"};
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id-a1", std::chrono::system_clock::time_point{}, &err));
    auto row = repo.GetByShortCode("abc", &err);
    BOOST_REQUIRE(row.has_value());
    BOOST_TEST(row->target_url == "https://example.com");
}
