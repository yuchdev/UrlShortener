#define BOOST_TEST_MODULE PostgresJsonbAttributesRoundtripTest
#include <boost/test/unit_test.hpp>

#include "psql_test_common.hpp"
#include "url_shortener/storage/postgres/PostgresSqlDialect.hpp"

BOOST_AUTO_TEST_CASE(attributes_roundtrip)
{
    auto repo = SqlMetadataRepository(std::make_shared<FakeFactory>(), std::make_shared<PostgresSqlDialect>(), std::make_shared<SqlMetadataRowMapper>());
    CreateLinkRequest req{"j1", "https://j"};
    req.attributes = {{"k1", "v1"}, {"k2", "v2"}};
    RepoError err = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink(req, "id-j1", std::chrono::system_clock::time_point{}, &err));
    auto out = repo.GetByShortCode("j1", &err);
    BOOST_REQUIRE(out.has_value());
    BOOST_TEST(out->attributes.at("k1") == "v1");
}
