#define BOOST_TEST_MODULE SqlRowMapperRoundtripTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/sql/SqlMetadataRowMapper.hpp"

BOOST_AUTO_TEST_CASE(row_mapper_roundtrip)
{
    SqlMetadataRowMapper m;
    CreateLinkRequest req;
    req.short_code = "abc123";
    req.target_url = "https://example.com";
    req.owner = "owner";
    req.attributes["k"] = "v";
    auto now = std::chrono::system_clock::time_point(std::chrono::seconds(42));

    auto row = m.ToCreateRow(req, "id1", now);
    LinkRecord out;
    RepoError err = RepoError::none;
    BOOST_REQUIRE(m.FromRow(row, &out, &err));
    BOOST_TEST(err == RepoError::none);
    BOOST_TEST(out.id == "id1");
    BOOST_TEST(out.short_code == "abc123");
    BOOST_TEST(out.attributes.at("k") == "v");
}
