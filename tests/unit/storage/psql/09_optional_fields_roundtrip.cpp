#define BOOST_TEST_MODULE PostgresOptionalFieldsRoundtripTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(optional_fields_roundtrip)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"opt", "https://o"};
    req.owner = "owner-a";
    req.expires_at = std::chrono::system_clock::time_point(std::chrono::seconds(100));
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id-o1", std::chrono::system_clock::time_point{}, &err));
    auto out = repo.GetByShortCode("opt", &err);
    BOOST_REQUIRE(out.has_value());
    BOOST_TEST(out->owner.value() == "owner-a");
    BOOST_TEST(out->expires_at.has_value());
}
