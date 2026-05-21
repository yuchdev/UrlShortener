#define BOOST_TEST_MODULE InMemoryMetadataExpirySemanticsTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(expiry_timestamp_can_be_set)
{
    InMemoryMetadataRepository repo;
    CreateLinkRequest req{"abc123", "https://a"};
    req.expires_at = std::chrono::system_clock::time_point{} - std::chrono::seconds(1);
    BOOST_REQUIRE(repo.CreateLink(req, "1", std::chrono::system_clock::time_point{}));
    auto rec = repo.GetByShortCode("abc123");
    BOOST_REQUIRE(rec.has_value());
    BOOST_TEST(rec->expires_at.has_value());
}
