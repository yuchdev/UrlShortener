#define BOOST_TEST_MODULE InMemoryMetadataUpdateTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(update_existing_record)
{
    InMemoryMetadataRepository repo;
    auto now = std::chrono::system_clock::time_point{};
    BOOST_REQUIRE(repo.CreateLink({"abc123", "https://a"}, "1", now));
    auto rec = repo.GetByShortCode("abc123");
    BOOST_REQUIRE(rec.has_value());
    rec->target_url = "https://b";
    rec->is_active = false;
    BOOST_REQUIRE(repo.UpdateLink(*rec));
    auto updated = repo.GetByShortCode("abc123");
    BOOST_REQUIRE(updated.has_value());
    BOOST_TEST(updated->target_url == "https://b");
    BOOST_TEST(!updated->is_active);
}
