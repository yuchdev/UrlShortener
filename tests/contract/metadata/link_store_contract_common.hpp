#pragma once

/**
 * @file link_store_contract_common.hpp
 * @brief Shared "link store contract" exercised against
 * `app::RepositoryLinkStore` over two backends (in-memory and a real
 * SQLite file), subtask 02.0/06.
 *
 * The existing `tests/contract/metadata` suite targets
 * `::IMetadataRepository`; this header targets the application-layer
 * `app::ILinkStore` as implemented by `RepositoryLinkStore`, so it proves
 * the codec round-trip, cache-aside invalidation observed through
 * read-your-writes, the write-behind redirect counters, and (for a
 * persistent backend) that every field survives a *new* store instance
 * over the same backing data — the property the CLI needs (threat model
 * subtask-06 requirement).
 *
 * The including translation unit must define `BOOST_TEST_MODULE` and
 * include `<boost/test/unit_test.hpp>` before this header. All helpers are
 * `inline` so the two thin instantiation files can both include it.
 *
 * The fixture hands back a *concrete* `RepositoryLinkStore` rather than an
 * `ILinkStore` (a small deviation from the spec's typed signature) so the
 * contract can drive the deterministic `flush_stats()` hook that case 8
 * requires — no timer, no sleep.
 */

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/i_metadata_repository.hpp>

/**
 * @brief Backend-independent wiring handed to `run_link_store_contract`.
 *
 * `make_store` builds a *fresh* `RepositoryLinkStore` (new L1 cache,
 * new store object) over the *same* backing data every time it is
 * called. For the in-memory fixture that backing data is one shared
 * `::IMetadataRepository` (models "same process"); for the SQLite
 * fixture it is a temp file re-opened by a new repository (models "next
 * process"), so `persistent` is `true` there.
 */
struct LinkStoreFixture
{
    ///< Clock shared by every store and its L1 cache; must outlive them
    ///< all (the store/cache hold it by reference). Advancing it is not
    ///< required — redirect timestamps are read from it deterministically.
    std::shared_ptr<ManualClock> clock;

    ///< Builds a new store over the same backing data. Repeated calls
    ///< model separate store instances (and, for SQLite, separate
    ///< processes) sharing one database.
    std::function<std::unique_ptr<url_shortener::app::RepositoryLinkStore>()>
        make_store;

    ///< `true` when data written through one `make_store()` survives into
    ///< the next (SQLite file); `false` for a same-process shared repo.
    bool persistent = false;

    ///< Optional teardown (e.g. remove the SQLite file); may be empty.
    std::function<void()> cleanup;
};

/**
 * @brief Builds a fully-populated `Link` so the codec round-trip is
 * exercised on every field, not just the required minimum.
 *
 * `updated_at` is set equal to `created_at` because the create path
 * stores `updated_at == created_at == parse(created_at)`; keeping them
 * equal lets case 1 assert field-for-field equality after a create.
 * @param slug Slug/id stem for the link (`id == "id-" + slug`).
 * @return A link with every optional field present.
 */
inline url_shortener::Link make_sample_link(const std::string& slug)
{
    url_shortener::Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug + "/path?q=1";
    link.created_at = "2026-01-01T00:00:00Z";
    link.updated_at = link.created_at;
    link.expires_at = "2030-06-15T12:30:00Z";
    link.enabled = true;
    link.tags = {"alpha", "beta"};
    link.metadata = {{"team", "growth"}, {"region", "emea"}};
    url_shortener::Link::Campaign campaign;
    campaign.name = "spring";
    campaign.source = "newsletter";
    campaign.medium = "email";
    campaign.term = "cpp";
    campaign.content = "variant-a";
    campaign.id = "cmp-1";
    link.campaign = campaign;
    link.redirect_type = url_shortener::RedirectType::permanent;
    return link;
}

/**
 * @brief Asserts every persisted `Link` field of @p actual equals
 * @p expected (stats excluded — those are checked explicitly where
 * relevant).
 * @param expected Link that was written.
 * @param actual Link that was read back.
 */
inline void check_links_equal(const url_shortener::Link& expected,
                              const url_shortener::Link& actual)
{
    BOOST_CHECK_EQUAL(actual.id, expected.id);
    BOOST_CHECK_EQUAL(actual.slug, expected.slug);
    BOOST_CHECK_EQUAL(actual.target_url, expected.target_url);
    BOOST_CHECK_EQUAL(actual.created_at, expected.created_at);
    BOOST_CHECK_EQUAL(actual.updated_at, expected.updated_at);
    BOOST_CHECK(actual.expires_at == expected.expires_at);
    BOOST_CHECK(actual.deleted_at == expected.deleted_at);
    BOOST_CHECK_EQUAL(actual.enabled, expected.enabled);
    BOOST_CHECK(actual.tags == expected.tags);
    BOOST_CHECK(actual.metadata == expected.metadata);
    BOOST_CHECK(actual.redirect_type == expected.redirect_type);

    BOOST_REQUIRE_EQUAL(actual.campaign.has_value(),
                        expected.campaign.has_value());
    if (expected.campaign.has_value()) {
        const auto& e = *expected.campaign;
        const auto& a = *actual.campaign;
        BOOST_CHECK(a.name == e.name);
        BOOST_CHECK(a.source == e.source);
        BOOST_CHECK(a.medium == e.medium);
        BOOST_CHECK(a.term == e.term);
        BOOST_CHECK(a.content == e.content);
        BOOST_CHECK(a.id == e.id);
    }
}

/**
 * @brief Runs the shared link-store contract against @p fixture.
 *
 * Persistent-only cases (2 and 8, and the reopen legs of 4/5) are gated
 * on `fixture.persistent`; the non-persistent in-memory instantiation
 * therefore runs cases 1, 3-7, 9, 10 while the SQLite instantiation runs
 * all ten. Every case uses a distinct slug so the shared backing data of
 * one run never cross-contaminates another case.
 * @param fixture Backend wiring under test.
 */
inline void run_link_store_contract(const LinkStoreFixture& fixture)
{
    using url_shortener::Link;
    using url_shortener::app::AppError;
    using url_shortener::app::AppErrorCode;

    // --- Case 1: create then findBySlug in the same store: fields equal. ---
    BOOST_TEST_CONTEXT("case 1: create then findBySlug (same store)")
    {
        auto store = fixture.make_store();
        const Link link = make_sample_link("case01");
        AppError error;
        BOOST_REQUIRE_MESSAGE(store->create(link, &error),
                              "create failed: " << error.detail);
        const auto read = store->findBySlug("case01");
        BOOST_REQUIRE(read.has_value());
        check_links_equal(link, *read);
    }

    // --- Case 2 (persistent only): findBySlug through a SECOND instance. ---
    if (fixture.persistent) {
        BOOST_TEST_CONTEXT("case 2: findBySlug through a second store instance")
        {
            {
                auto writer = fixture.make_store();
                BOOST_REQUIRE(writer->create(make_sample_link("case02")));
            }
            auto reader = fixture.make_store();
            const auto read = reader->findBySlug("case02");
            BOOST_REQUIRE(read.has_value());
            check_links_equal(make_sample_link("case02"), *read);
        }
    }

    // --- Case 3: duplicate slug -> slug_conflict; first record unchanged. ---
    BOOST_TEST_CONTEXT("case 3: duplicate slug is slug_conflict")
    {
        auto store = fixture.make_store();
        BOOST_REQUIRE(store->create(make_sample_link("case03")));

        Link dup = make_sample_link("case03");
        dup.id = "id-case03-other";
        dup.target_url = "https://evil.example/other";
        AppError error;
        BOOST_CHECK(!store->create(dup, &error));
        BOOST_CHECK(error.code == AppErrorCode::slug_conflict);
        BOOST_CHECK_EQUAL(error.detail, "slug already in use");

        // First record is untouched: its original target still resolves.
        auto reader = fixture.make_store();
        const auto read = reader->findBySlug("case03");
        BOOST_REQUIRE(read.has_value());
        BOOST_CHECK_EQUAL(read->target_url,
                          "https://example.com/case03/path?q=1");
        BOOST_CHECK_EQUAL(read->id, "id-case03");
    }

    // --- Case 4: update hostile tags/metadata/campaign/expiry; re-read. ---
    BOOST_TEST_CONTEXT("case 4: update with hostile values round-trips")
    {
        auto store = fixture.make_store();
        BOOST_REQUIRE(store->create(make_sample_link("case04")));

        Link updated = make_sample_link("case04");
        updated.updated_at = "2027-03-03T03:03:03Z";
        updated.enabled = false;
        updated.expires_at = "2031-12-31T23:59:59Z";
        // Separators the composite attribute encoding must percent-escape.
        updated.tags = {"a.b", "c&d", "e=f", "g,h"};
        // Metadata keys containing '.' must survive (they live inside the
        // lnk_meta value, percent-encoded, not as ptree-splitting keys).
        updated.metadata = {{"a.b.c", "x=y&z"}, {"plain", "..."}};
        Link::Campaign campaign;
        campaign.name = "name=with&sep";
        campaign.source = "src.dot";
        campaign.medium = "m,comma";
        updated.campaign = campaign;
        AppError error;
        BOOST_REQUIRE_MESSAGE(store->update(updated, &error),
                              "update failed: " << error.detail);

        auto reader = fixture.make_store();
        const auto read = reader->findBySlug("case04");
        BOOST_REQUIRE(read.has_value());
        check_links_equal(updated, *read);
    }

    // --- Case 5: soft delete then restore; both survive a re-read. ---
    BOOST_TEST_CONTEXT("case 5: soft delete then restore")
    {
        auto store = fixture.make_store();
        BOOST_REQUIRE(store->create(make_sample_link("case05")));

        // Soft delete: deleted_at set. The SQL schema has no deleted_at
        // column, so this exercises the attr_deleted codec path.
        Link deleted = make_sample_link("case05");
        deleted.updated_at = "2027-01-01T00:00:00Z";
        deleted.deleted_at = "2027-01-01T00:00:00Z";
        BOOST_REQUIRE(store->update(deleted));
        {
            auto reader = fixture.make_store();
            const auto read = reader->findBySlug("case05");
            BOOST_REQUIRE(read.has_value());
            BOOST_REQUIRE(read->deleted_at.has_value());
            BOOST_CHECK_EQUAL(*read->deleted_at, "2027-01-01T00:00:00Z");
        }

        // Restore: clear deleted_at.
        Link restored = make_sample_link("case05");
        restored.updated_at = "2027-02-02T00:00:00Z";
        restored.deleted_at.reset();
        BOOST_REQUIRE(store->update(restored));
        {
            auto reader = fixture.make_store();
            const auto read = reader->findBySlug("case05");
            BOOST_REQUIRE(read.has_value());
            BOOST_CHECK(!read->deleted_at.has_value());
        }
    }

    // --- Case 6: findById returns the link; unknown id -> nullopt. ---
    BOOST_TEST_CONTEXT("case 6: findById hit and miss")
    {
        auto store = fixture.make_store();
        BOOST_REQUIRE(store->create(make_sample_link("case06")));

        auto reader = fixture.make_store();
        const auto found = reader->findById("id-case06");
        BOOST_REQUIRE(found.has_value());
        BOOST_CHECK_EQUAL(found->slug, "case06");
        BOOST_CHECK(!reader->findById("id-does-not-exist").has_value());
    }

    // --- Case 7: slugExists true / false. ---
    BOOST_TEST_CONTEXT("case 7: slugExists true and false")
    {
        auto store = fixture.make_store();
        BOOST_REQUIRE(store->create(make_sample_link("case07")));

        auto reader = fixture.make_store();
        BOOST_CHECK(reader->slugExists("case07"));
        BOOST_CHECK(!reader->slugExists("case07-absent"));
    }

    // --- Case 8 (persistent only): recordRedirect + flush_stats persist. ---
    if (fixture.persistent) {
        BOOST_TEST_CONTEXT("case 8: recordRedirect + flush_stats then reopen")
        {
            Link served;
            {
                auto store = fixture.make_store();
                BOOST_REQUIRE(store->create(make_sample_link("case08")));
                const auto loaded = store->findBySlug("case08");
                BOOST_REQUIRE(loaded.has_value());
                served = *loaded;

                // Three redirects accumulate a +3 delta (write-behind, no
                // flusher thread started); flush_stats persists it with a
                // real SQL round trip.
                for (int i = 0; i < 3; ++i) {
                    BOOST_CHECK(store->recordRedirect(served));
                }
                store->flush_stats();
            }

            auto reader = fixture.make_store();
            const auto read = reader->findBySlug("case08");
            BOOST_REQUIRE(read.has_value());
            // Counters persisted across the reopen.
            BOOST_CHECK_EQUAL(read->stats.total_redirects, 3u);
            BOOST_CHECK_EQUAL(read->stats.redirects_24h, 3u);
            BOOST_CHECK_EQUAL(read->stats.redirects_7d, 3u);
            BOOST_CHECK(read->stats.last_accessed_at.has_value());
            // Every other field is untouched by the stats-only flush.
            Link expected = make_sample_link("case08");
            check_links_equal(expected, *read);
        }
    }

    // --- Case 9: 8 threads create one slug; exactly one wins. ---
    BOOST_TEST_CONTEXT("case 9: concurrent create, exactly one winner")
    {
        auto store = fixture.make_store();
        constexpr int k_threads = 8;
        std::atomic<int> wins {0};
        std::atomic<int> conflicts {0};
        std::vector<std::thread> workers;
        workers.reserve(k_threads);
        for (int i = 0; i < k_threads; ++i) {
            workers.emplace_back(
                [&store, &wins, &conflicts, i]
                {
                    Link link = make_sample_link("case09");
                    // Distinct id per thread so the only collision is the
                    // shared slug (not the id).
                    link.id = "id-case09-" + std::to_string(i);
                    AppError error;
                    if (store->create(link, &error)) {
                        ++wins;
                    }
                    else if (error.code == AppErrorCode::slug_conflict) {
                        ++conflicts;
                    }
                });
        }
        for (auto& worker : workers) {
            worker.join();
        }
        BOOST_CHECK_EQUAL(wins.load(), 1);
        BOOST_CHECK_EQUAL(conflicts.load(), k_threads - 1);
    }

    // --- Case 10: 200 links; findById for the last one succeeds. ---
    BOOST_TEST_CONTEXT("case 10: bulk create then findById the last")
    {
        auto store = fixture.make_store();
        constexpr int k_count = 200;
        for (int i = 0; i < k_count; ++i) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%03d", i);
            Link link = make_sample_link(std::string("bulk") + buf);
            BOOST_REQUIRE(store->create(link));
        }
        auto reader = fixture.make_store();
        const auto last = reader->findById("id-bulk199");
        BOOST_REQUIRE(last.has_value());
        BOOST_CHECK_EQUAL(last->slug, "bulk199");
    }

    if (fixture.cleanup) {
        fixture.cleanup();
    }
}
