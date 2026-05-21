#define BOOST_TEST_MODULE InMemoryMetadataExistsTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(exists_consistent_with_get)
{
    InMemoryMetadataRepository repo;
    BOOST_TEST(!repo.Exists("abc"));
    BOOST_REQUIRE(repo.CreateLink({"abc", "https://a"}, "1", std::chrono::system_clock::time_point{}));
    BOOST_TEST(repo.Exists("abc"));
    BOOST_TEST(repo.GetByShortCode("abc").has_value());
    BOOST_TEST(repo.DeleteLink("abc"));
    BOOST_TEST(!repo.Exists("abc"));
}
