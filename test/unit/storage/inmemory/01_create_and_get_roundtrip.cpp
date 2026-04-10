#define BOOST_TEST_MODULE InMemoryMetadataCreateGetRoundtripTest
#include <boost/test/unit_test.hpp>

#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"

BOOST_AUTO_TEST_CASE(create_get_roundtrip)
{
    InMemoryMetadataRepository repo;
    CreateLinkRequest req{"abc123", "https://example.com"};
    BOOST_REQUIRE(repo.CreateLink(req, "id1", std::chrono::system_clock::time_point{}));
    auto got = repo.GetByShortCode("abc123");
    BOOST_REQUIRE(got.has_value());
    BOOST_TEST(got->id == "id1");
}
