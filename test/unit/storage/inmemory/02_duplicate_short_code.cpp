#define BOOST_TEST_MODULE InMemoryMetadataDuplicateTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(duplicate_short_code_returns_already_exists)
{
    InMemoryMetadataRepository repo;
    RepoError error = RepoError::none;
    BOOST_REQUIRE(repo.CreateLink({"abc123", "https://a"}, "1", std::chrono::system_clock::time_point{}, &error));
    BOOST_TEST(!repo.CreateLink({"abc123", "https://b"}, "2", std::chrono::system_clock::time_point{}, &error));
    BOOST_TEST(error == RepoError::already_exists);
}
