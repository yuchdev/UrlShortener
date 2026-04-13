#define BOOST_TEST_MODULE PostgresUpdateExistingTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(update_existing_record)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"u1", "https://before"};
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id-u1", std::chrono::system_clock::time_point{}, &err));
    auto rec = repo.GetByShortCode("u1", &err);
    rec->target_url = "https://after";
    BOOST_REQUIRE(repo.UpdateLink(*rec, &err));
    auto check = repo.GetByShortCode("u1", &err);
    BOOST_TEST(check->target_url == "https://after");
}
