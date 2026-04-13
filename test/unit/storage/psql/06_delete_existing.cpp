#define BOOST_TEST_MODULE PostgresDeleteExistingTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(delete_existing_record)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"d1", "https://t"};
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id-d1", std::chrono::system_clock::time_point{}, &err));
    BOOST_REQUIRE(repo.DeleteLink("d1", &err));
    BOOST_TEST(!repo.GetByShortCode("d1", &err).has_value());
}
