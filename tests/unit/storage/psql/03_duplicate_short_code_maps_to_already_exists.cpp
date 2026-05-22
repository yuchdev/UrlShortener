#define BOOST_TEST_MODULE PostgresDuplicateMapsAlreadyExistsTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(duplicate_short_code_maps_to_already_exists)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"dup", "https://e.com"};
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id-1", std::chrono::system_clock::time_point{}, &err));
    BOOST_TEST(!repo.CreateLink(req, "id-2", std::chrono::system_clock::time_point{}, &err));
    BOOST_TEST(err == RepoError::already_exists);
}
