#define BOOST_TEST_MODULE InMemoryMetadataThreadSafetySmokeTest
#include <boost/test/unit_test.hpp>
#include <thread>
#include <vector>
#include "url_shortener/storage/inmemory/InMemoryMetadataRepository.hpp"
BOOST_AUTO_TEST_CASE(concurrent_create_one_wins_and_reads_safe)
{
    InMemoryMetadataRepository repo;
    std::atomic<int> winners{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 16; ++i) {
        threads.emplace_back([&]() {
            if (repo.CreateLink({"abc123", "https://a"}, "1", std::chrono::system_clock::time_point{})) {
                ++winners;
            }
            (void)repo.GetByShortCode("abc123");
        });
    }
    for (auto& t : threads) { t.join(); }
    BOOST_TEST(winners.load() == 1);
}
