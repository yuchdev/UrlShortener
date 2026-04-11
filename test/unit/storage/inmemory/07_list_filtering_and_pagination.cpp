#define BOOST_TEST_MODULE InMemoryMetadataListFilteringPaginationTest
#include <boost/test/unit_test.hpp>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(list_filtering_and_pagination)
{
    InMemoryMetadataRepository repo;
    auto t = std::chrono::system_clock::time_point{};
    CreateLinkRequest a{"a11", "https://a"}; a.owner = "o1";
    CreateLinkRequest b{"b22", "https://b"}; b.owner = "o1"; b.is_active = false;
    CreateLinkRequest c{"c33", "https://c"}; c.owner = "o2";
    BOOST_REQUIRE(repo.CreateLink(a, "1", t));
    BOOST_REQUIRE(repo.CreateLink(b, "2", t + std::chrono::seconds(1)));
    BOOST_REQUIRE(repo.CreateLink(c, "3", t + std::chrono::seconds(2)));
    ListLinksQuery q; q.owner = "o1"; q.include_inactive = false;
    auto only_active = repo.ListLinks(q);
    BOOST_TEST(only_active.size() == 1);
    q.include_inactive = true; q.offset = 1; q.limit = 1;
    auto paged = repo.ListLinks(q);
    BOOST_TEST(paged.size() == 1);
}
