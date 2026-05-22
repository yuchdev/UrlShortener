#define BOOST_TEST_MODULE MetadataContractConcurrentCreateOneWinsTest
#include <boost/test/unit_test.hpp>
#include <atomic>
#include <thread>
#include <vector>
#include "metadata_contract_common.hpp"
BOOST_AUTO_TEST_CASE(contract_concurrent_create_one_wins)
{
    for (auto& h : MakeMetadataHarnesses()) {
    std::atomic<int> winners{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 32; ++i) {
        threads.emplace_back([&]() {
            if (h.repo->CreateLink(BasicRequest("abc123"), "id", h.clock->now())) {
                ++winners;
            }
        });
    }
    for (auto& t : threads) { t.join(); }
    BOOST_TEST(winners.load() == 1);
}
}
