#define BOOST_TEST_MODULE InMemoryMetadataDeleteTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(delete_existing_and_unknown_is_idempotent)
{
    InMemoryMetadataRepository repo;
    BOOST_REQUIRE(repo.CreateLink({"abc123", "https://a"}, "1", std::chrono::system_clock::time_point{}));
    BOOST_TEST(repo.DeleteLink("abc123"));
    BOOST_TEST(!repo.GetByShortCode("abc123").has_value());
    BOOST_TEST(repo.DeleteLink("abc123"));
}
