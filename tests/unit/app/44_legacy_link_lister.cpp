/**
 * @file 44_legacy_link_lister.cpp
 * @brief Unit tests for app::LegacyLinkLister over the legacy, `Link`-based
 * in-memory `url_shortener::IMetadataRepository` (subtask 13).
 *
 * Runs the implementation- and backend-agnostic case table
 * (`link_lister_cases.hpp`, shared verbatim with subtask 12's
 * `43_repository_link_lister.cpp`) against the legacy snapshot lister, so both
 * listers are proven to return identical slugs/order/total for every case, and
 * every listed item equals its `GetLink` projection. Because the legacy
 * repository is normally a process singleton, every test constructs its OWN
 * `url_shortener::InMemoryMetadataRepository` instance and never touches the
 * `linkRepository()` global, so other tests see an unchanged singleton.
 *
 * Also covers: an empty repository lists nothing; `snapshot()` returns
 * soft-deleted and disabled links; `snapshot()` is an independent copy (a later
 * mutation of the repository does not change an already-returned vector); and a
 * concurrent `create` while listing does not crash.
 */
#define BOOST_TEST_MODULE LegacyLinkLister
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/legacy_link_lister.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/app/link_lister.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/link_repository.hpp>

#include "app_test_fakes.hpp"
#include "link_lister_cases.hpp"

using namespace url_shortener;

namespace
{

/**
 * @brief Owns a legacy repository plus the service wiring a `LegacyLinkLister`
 * needs, so all referenced objects outlive the lister for a test's duration.
 *
 * The lister reads the legacy `url_shortener::InMemoryMetadataRepository`;
 * `GetLink` reads a `FakeLinkStore` seeded from the same `seed_links()`, so the
 * two are projected from identical link data and must agree.
 */
struct LegacyFixture
{
    url_shortener::InMemoryMetadataRepository repo;
    test_fakes::FakeLinkStore fake_store;
    test_fakes::FakeLinkStatsReader stats_reader;
    ServerConfig config;
    std::unique_ptr<app::LinkCommandService> service;
    std::unique_ptr<app::LegacyLinkLister> lister;

    LegacyFixture()
        : config(test_fakes::makeTestConfig())
    {
        service = std::make_unique<app::LinkCommandService>(
            fake_store, stats_reader, config);
        lister = std::make_unique<app::LegacyLinkLister>(repo, *service);
    }
};

/**
 * @brief Seeds @p repo with the shared seed links directly (the legacy
 * repository stores `Link` values verbatim, so no codec step is needed).
 * @param repo Legacy repository to seed in place.
 */
void seed_legacy_repo(url_shortener::IMetadataRepository& repo)
{
    for (const Link& link : link_lister_cases::seed_links()) {
        RepoError error = RepoError::not_found;
        BOOST_REQUIRE_MESSAGE(repo.create(link, &error),
                              "create " << link.slug);
    }
}

/**
 * @brief Seeds @p store with the shared seed links so `GetLink` can project
 * the same links the lister returns.
 * @param store Fake store to seed in place.
 */
void seed_fake_store(test_fakes::FakeLinkStore& store)
{
    for (const Link& link : link_lister_cases::seed_links()) {
        store.seed(link);
    }
}

/**
 * @brief Extracts the slugs of a page's items in order.
 */
std::vector<std::string> slugs_of(const app::LinkListPage& page)
{
    std::vector<std::string> out;
    out.reserve(page.items.size());
    for (const auto& item : page.items) {
        out.push_back(item.slug);
    }
    return out;
}

}  // namespace

/**
 * [Unit][App] The shared case table returns identical results over the legacy
 * repository, proving the legacy and repository listers are equivalent.
 *
 * Scenario:
 *   Given the canonical seed links in a local legacy in-memory repository.
 *   When each shared browse case (the exact table used for the repository
 *   lister) runs.
 *   Then the slugs, order and total match the case's expectation and every
 *   item equals its GetLink projection.
 *
 * If this breaks, first check:
 *   - LegacyLinkLister reusing build_list_links_query/ApplyListQuery (one
 *     filter implementation) rather than re-filtering.
 *   - link_to_record/record_to_link round-trip preserving the seed links.
 */
BOOST_AUTO_TEST_CASE(shared_cases_legacy)
{
    LegacyFixture fx;
    seed_legacy_repo(fx.repo);
    seed_fake_store(fx.fake_store);

    for (const auto& test_case : link_lister_cases::lister_cases()) {
        BOOST_TEST_CONTEXT("legacy / " << test_case.name)
        {
            const auto result = fx.lister->list(test_case.query);
            BOOST_REQUIRE(result.ok());
            const auto& page = *result.value;

            BOOST_CHECK_EQUAL(page.total, test_case.expected_total);
            BOOST_CHECK_EQUAL(page.offset, test_case.query.offset);
            BOOST_CHECK_EQUAL(page.limit, test_case.query.limit);

            const auto actual = slugs_of(page);
            BOOST_REQUIRE_EQUAL(actual.size(), test_case.expected_slugs.size());
            for (std::size_t i = 0; i < actual.size(); ++i) {
                BOOST_CHECK_EQUAL(actual[i], test_case.expected_slugs[i]);
            }

            // Every listed item is byte-identical to a single-link read.
            for (const auto& item : page.items) {
                app::GetLinkQuery query;
                query.by = app::GetLinkBy::slug;
                query.value = item.slug;
                const auto got = fx.service->GetLink(query);
                BOOST_REQUIRE_MESSAGE(got.ok(), "GetLink(" << item.slug << ")");
                BOOST_CHECK_EQUAL(app::serializeLinkViewJson(item),
                                  app::serializeLinkViewJson(*got.value));
            }
        }
    }
}

/**
 * [Unit][App] An empty legacy repository lists nothing with total 0.
 *
 * Scenario:
 *   Given an empty legacy in-memory repository.
 *   When status=all is listed.
 *   Then items is empty and total is 0.
 *
 * If this breaks, first check snapshot() over an empty repository and the
 * LinkListPage default total.
 */
BOOST_AUTO_TEST_CASE(empty_repository_lists_nothing)
{
    LegacyFixture fx;

    app::LinkListQuery q;
    q.status = app::list_status::all;
    const auto r = fx.lister->list(q);
    BOOST_REQUIRE(r.ok());
    BOOST_CHECK(r.value->items.empty());
    BOOST_CHECK_EQUAL(r.value->total, 0u);
}

/**
 * [Unit][App] limit 0/501 and an oversized q are rejected as invalid_field
 * naming the field only, matching the repository lister.
 *
 * Scenario:
 *   Given a seeded legacy lister.
 *   When a query with an out-of-range limit or oversized q is listed.
 *   Then the result is invalid_field with detail "limit"/"q" and no value.
 *
 * If this breaks, first check that list() validates with
 * validate_link_list_query before snapshotting.
 */
BOOST_AUTO_TEST_CASE(query_bounds_rejected)
{
    LegacyFixture fx;
    seed_legacy_repo(fx.repo);

    {
        app::LinkListQuery q;
        q.limit = 0;
        const auto r = fx.lister->list(q);
        BOOST_REQUIRE(!r.ok());
        BOOST_CHECK_EQUAL(r.error.code, app::AppErrorCode::invalid_field);
        BOOST_CHECK_EQUAL(r.error.detail, "limit");
    }
    {
        app::LinkListQuery q;
        q.limit = app::link_list_max_limit + 1;
        const auto r = fx.lister->list(q);
        BOOST_REQUIRE(!r.ok());
        BOOST_CHECK_EQUAL(r.error.code, app::AppErrorCode::invalid_field);
        BOOST_CHECK_EQUAL(r.error.detail, "limit");
    }
    {
        app::LinkListQuery q;
        q.q = std::string(app::link_list_max_query_length + 1, 'x');
        const auto r = fx.lister->list(q);
        BOOST_REQUIRE(!r.ok());
        BOOST_CHECK_EQUAL(r.error.code, app::AppErrorCode::invalid_field);
        BOOST_CHECK_EQUAL(r.error.detail, "q");
    }
}

/**
 * [Unit][App] snapshot() returns every lifecycle state, including soft-deleted
 * and disabled links.
 *
 * Scenario:
 *   Given the canonical seed links (one of every status) in a local repository.
 *   When snapshot() is taken.
 *   Then it carries all six links, including the disabled "bravo" and the
 *   soft-deleted "delta".
 *
 * If this breaks, first check InMemoryMetadataRepository::snapshot copying
 * every by_id_ value without a status filter.
 */
BOOST_AUTO_TEST_CASE(snapshot_includes_deleted_and_disabled)
{
    url_shortener::InMemoryMetadataRepository repo;
    seed_legacy_repo(repo);

    const std::vector<Link> snap = repo.snapshot();
    BOOST_REQUIRE_EQUAL(snap.size(), link_lister_cases::seed_links().size());

    bool has_bravo = false;  // disabled
    bool has_delta = false;  // soft-deleted
    for (const Link& link : snap) {
        if (link.slug == "bravo") {
            has_bravo = true;
            BOOST_CHECK(!link.enabled);
        }
        if (link.slug == "delta") {
            has_delta = true;
            BOOST_CHECK(link.deleted_at.has_value());
        }
    }
    BOOST_CHECK(has_bravo);
    BOOST_CHECK(has_delta);
}

/**
 * [Unit][App] snapshot() returns an independent copy: mutating the repository
 * afterwards does not change an already-returned vector.
 *
 * Scenario:
 *   Given a repository with one link and a snapshot already taken.
 *   When a second link is created in the repository.
 *   Then the earlier snapshot still has exactly one element.
 *
 * If this breaks, first check snapshot() returning copies of by_id_ values
 * rather than references/handles into the live map.
 */
BOOST_AUTO_TEST_CASE(snapshot_is_an_independent_copy)
{
    url_shortener::InMemoryMetadataRepository repo;

    Link first;
    first.id = "id-one";
    first.slug = "one";
    first.target_url = "https://one.example.com";
    first.enabled = true;
    first.created_at = link_lister_cases::lister_case_timestamp(10);
    first.updated_at = first.created_at;
    RepoError e1 = RepoError::not_found;
    BOOST_REQUIRE(repo.create(first, &e1));

    const std::vector<Link> snap = repo.snapshot();
    BOOST_REQUIRE_EQUAL(snap.size(), 1u);

    Link second;
    second.id = "id-two";
    second.slug = "two";
    second.target_url = "https://two.example.com";
    second.enabled = true;
    second.created_at = link_lister_cases::lister_case_timestamp(20);
    second.updated_at = second.created_at;
    RepoError e2 = RepoError::not_found;
    BOOST_REQUIRE(repo.create(second, &e2));

    // The previously-returned vector is unaffected by the new create.
    BOOST_CHECK_EQUAL(snap.size(), 1u);
    BOOST_CHECK_EQUAL(snap[0].slug, "one");
    // And a fresh snapshot now sees both.
    BOOST_CHECK_EQUAL(repo.snapshot().size(), 2u);
}

/**
 * [Unit][App] Concurrent create while listing does not crash.
 *
 * Scenario:
 *   Given a seeded legacy lister.
 *   When one thread creates fresh links while another lists repeatedly
 *   (bounded iterations).
 *   Then both threads complete without a crash or data race (shared-lock
 *   snapshot vs. unique-lock create).
 *
 * If this breaks, first check snapshot() taking only a shared lock and copying
 * before returning (no decoding under the lock).
 */
BOOST_AUTO_TEST_CASE(concurrent_create_while_listing)
{
    LegacyFixture fx;
    seed_legacy_repo(fx.repo);

    constexpr int kIterations = 200;
    std::atomic<bool> go {false};

    std::thread writer(
        [&]
        {
            while (!go.load()) {}
            for (int i = 0; i < kIterations; ++i) {
                Link l;
                l.id = "cc-id-" + std::to_string(i);
                l.slug = "cc-" + std::to_string(i);
                l.target_url = "https://cc.example.com/" + std::to_string(i);
                l.enabled = true;
                l.created_at = link_lister_cases::lister_case_timestamp(i);
                l.updated_at = l.created_at;
                RepoError error = RepoError::not_found;
                fx.repo.create(l, &error);
            }
        });

    std::thread reader(
        [&]
        {
            while (!go.load()) {}
            app::LinkListQuery q;
            q.status = app::list_status::all;
            q.limit = app::link_list_max_limit;
            for (int i = 0; i < kIterations; ++i) {
                const auto r = fx.lister->list(q);
                BOOST_REQUIRE(r.ok());
            }
        });

    go.store(true);
    writer.join();
    reader.join();

    // Final state is self-consistent: every created link is listable.
    app::LinkListQuery q;
    q.status = app::list_status::all;
    q.limit = app::link_list_max_limit;
    const auto r = fx.lister->list(q);
    BOOST_REQUIRE(r.ok());
    BOOST_CHECK_EQUAL(r.value->total,
                      link_lister_cases::seed_links().size() + kIterations);
}
