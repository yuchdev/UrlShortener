/**
 * @file 16_repository_link_store_contract.cpp
 * @brief In-memory instantiation of the shared link-store contract
 * (subtask 02.0/06).
 *
 * Wires `run_link_store_contract` to a single `::IMetadataRepository`
 * shared by every `make_store()` call (models "same process"), so the
 * store is non-persistent: the backing data lives only as long as the
 * shared repo. This instantiation runs the non-persistent subset
 * (cases 1, 3-7, 9, 10); the reopen/stats cases (2, 8) are gated to the
 * SQLite instantiation.
 */
#define BOOST_TEST_MODULE RepositoryLinkStoreContractInMemory
#include <chrono>
#include <memory>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/storage/inmemory/in_memory_metadata_repository.hpp>

#include "link_store_contract_common.hpp"

/**
 * [Contract][App] RepositoryLinkStore satisfies the link-store contract
 * over the in-memory `::IMetadataRepository` (non-persistent subset).
 *
 * Scenario:
 *   Given one shared in-memory repository and a fresh RepositoryLinkStore
 *   (with its own L1 cache) built per make_store() call.
 *   When the shared contract runs cases 1, 3-7, 9 and 10.
 *   Then create/findBySlug/findById/slugExists/update round-trip every
 *   field, duplicate slugs conflict without mutating the first record,
 *   concurrent creates of one slug produce exactly one winner, and a
 *   200-link bulk load remains findable by id.
 *
 * If this breaks, first check:
 *   - the codec round-trip (link_record_codec) for the field that differs.
 *   - RepositoryLinkStore::create/update cache invalidation and the repo
 *     uniqueness mapping to slug_conflict.
 */
BOOST_AUTO_TEST_CASE(repository_link_store_contract_inmemory)
{
    auto clock =
        std::make_shared<ManualClock>(std::chrono::system_clock::time_point {});
    auto repo = std::make_shared<InMemoryMetadataRepository>();

    LinkStoreFixture fixture;
    fixture.clock = clock;
    fixture.persistent = false;
    fixture.make_store = [repo, clock]()
    {
        // Each store gets its own L1 (ttl 0 = never expire) over the one
        // shared repository; this models separate store objects within a
        // single process sharing the authoritative in-memory store.
        auto l1 = std::make_shared<url_shortener::app::ExpiringLinkCache>(
            *clock, std::chrono::seconds(0));
        return std::make_unique<url_shortener::app::RepositoryLinkStore>(
            repo, nullptr, l1, *clock);
    };

    run_link_store_contract(fixture);
}
