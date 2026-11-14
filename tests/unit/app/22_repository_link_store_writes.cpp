/**
 * @file 22_repository_link_store_writes.cpp
 * @brief Unit tests for app::RepositoryLinkStore write side (subtask 04):
 * create/update codec mapping, `::RepoError` -> `AppErrorCode` mapping with
 * the exact legacy messages (C3/C4 message parity), both-tier cache
 * invalidation (L1 always on update, configured `::ICacheStore` best-effort
 * on success), read-your-writes, and the "cache failures never fail a
 * request" contract (a throwing `Delete` is contained).
 */
#define BOOST_TEST_MODULE RepositoryLinkStoreWrites
#include <chrono>
#include <memory>
#include <optional>
#include <string>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/expiring_link_cache.hpp>
#include <url_shortener/app/link_record_codec.hpp>
#include <url_shortener/app/repository_link_store.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/storage/errors/repo_error.hpp>

#include "repository_link_store_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;

namespace
{

/// Zero-TTL L1 so an L1 hit is an unconditional cache presence (no expiry).
std::shared_ptr<ExpiringLinkCache> makeL1(IClock& clock)
{
    return std::make_shared<ExpiringLinkCache>(clock, std::chrono::seconds(0));
}

}  // namespace

/**
 * [Unit][App] create happy path: the link is persisted with
 * created_at == updated_at == parsed link.created_at, is_active per the
 * enabled flag, and the packed lnk_* attributes present; L1 is populated.
 *
 * Scenario:
 *   Given an empty repository and an enabled link.
 *   When create is called once.
 *   Then CreateLink runs once, the stored record carries the parsed
 *   creation timestamp as both created_at and updated_at, is_active is
 *   true, and the schema/enabled attributes are packed.
 *
 * If this breaks, first check:
 *   - create passing parseRfc3339Zulu(link.created_at) as `now`.
 *   - create mapping the link through link_to_create_request.
 */
BOOST_AUTO_TEST_CASE(create_happy_path_persists_record)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    const Link link = makeLink("aaa");
    AppError error;
    BOOST_REQUIRE(store.create(link, &error));
    BOOST_CHECK_EQUAL(repo->create_calls, 1);

    const ::LinkRecord* record = repo->find("aaa");
    BOOST_REQUIRE(record != nullptr);
    BOOST_CHECK(record->is_active);  // enabled, not deleted
    const auto expected_now = parseRfc3339Zulu(link.created_at);
    BOOST_REQUIRE(expected_now.has_value());
    BOOST_CHECK(record->created_at == *expected_now);
    BOOST_CHECK(record->updated_at == *expected_now);
    BOOST_CHECK(record->created_at == record->updated_at);
    BOOST_CHECK_EQUAL(record->attributes.count(std::string(attr_schema)), 1u);
    BOOST_CHECK_EQUAL(record->attributes.count(std::string(attr_enabled)), 1u);

    // L1 was populated with a fresh copy: a read is served without a
    // repository GetByShortCode call.
    const auto cached = store.findBySlug("aaa");
    BOOST_REQUIRE(cached.has_value());
    BOOST_CHECK_EQUAL(cached->target_url, link.target_url);
    BOOST_CHECK_EQUAL(repo->get_calls, 0);
}

/**
 * [Unit][App] A disabled link is persisted with is_active == false.
 *
 * If this breaks, first check:
 *   - the codec computing is_active = enabled && !deleted.
 */
BOOST_AUTO_TEST_CASE(create_disabled_link_is_inactive)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    Link link = makeLink("off");
    link.enabled = false;
    BOOST_REQUIRE(store.create(link));

    const ::LinkRecord* record = repo->find("off");
    BOOST_REQUIRE(record != nullptr);
    BOOST_CHECK(!record->is_active);
}

/**
 * [Unit][App] A duplicate slug maps RepoError::already_exists to
 * slug_conflict / "slug already in use" and leaves L1 unchanged.
 *
 * If this breaks, first check:
 *   - map_repo_error's already_exists branch and its exact message.
 *   - create not populating L1 on a failed CreateLink.
 */
BOOST_AUTO_TEST_CASE(create_duplicate_slug_is_slug_conflict)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("aaa")));
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    AppError error;
    BOOST_CHECK(!store.create(makeLink("aaa"), &error));
    BOOST_CHECK(error.code == AppErrorCode::slug_conflict);
    BOOST_CHECK_EQUAL(error.detail, "slug already in use");
    BOOST_CHECK(!l1->get("aaa").has_value());  // L1 unchanged
}

/**
 * [Unit][App] A RepoError::invalid_argument (e.g. a 2-char slug the
 * repository rejects) maps to invalid_field.
 *
 * If this breaks, first check:
 *   - map_repo_error's invalid_argument branch.
 */
BOOST_AUTO_TEST_CASE(create_invalid_argument_is_invalid_field)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    // "ab" fails CreateLinkRequest::isValid() (short_code length < 3).
    Link link = makeLink("ab");
    AppError error;
    BOOST_CHECK(!store.create(link, &error));
    BOOST_CHECK(error.code == AppErrorCode::invalid_field);
    BOOST_CHECK_EQUAL(error.detail, "invalid link");
}

/**
 * [Unit][App] Transient and permanent backend errors both map to
 * storage_failure / "repository failure" (message parity with
 * LegacyLinkStore, C3/C4).
 *
 * If this breaks, first check:
 *   - map_repo_error's default branch and its exact message.
 */
BOOST_AUTO_TEST_CASE(create_backend_error_is_storage_failure)
{
    for (const auto err :
         {::RepoError::transient_failure, ::RepoError::permanent_failure})
    {
        ManualClock clock(kT0);
        auto repo = std::make_shared<FakeMetadataRepository>();
        repo->create_error = err;
        auto l1 = makeL1(clock);
        RepositoryLinkStore store(repo, nullptr, l1, clock);

        AppError error;
        BOOST_CHECK(!store.create(makeLink("aaa"), &error));
        BOOST_CHECK(error.code == AppErrorCode::storage_failure);
        BOOST_CHECK_EQUAL(error.detail, "repository failure");
    }
}

/**
 * [Unit][App] An unparseable created_at fails with internal and never
 * calls the repository.
 *
 * If this breaks, first check:
 *   - create parsing created_at and returning before CreateLink on
 *     failure.
 */
BOOST_AUTO_TEST_CASE(create_bad_created_at_is_internal_no_repo_call)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    Link link = makeLink("aaa");
    link.created_at = "not-a-timestamp";
    AppError error;
    BOOST_CHECK(!store.create(link, &error));
    BOOST_CHECK(error.code == AppErrorCode::internal);
    BOOST_CHECK_EQUAL(repo->create_calls, 0);
}

/**
 * [Unit][App] update happy path: new tags/enabled/deleted_at reach the
 * stored record (lnk_deleted present, is_active == false), L1 is erased,
 * and the configured ICacheStore Delete is called exactly once.
 *
 * Scenario:
 *   Given an existing link cached in L1.
 *   When update soft-deletes and disables it with a new tag.
 *   Then the stored record is inactive with the deleted/tags attributes,
 *   the L1 entry is gone, and the configured cache saw one Delete.
 *
 * If this breaks, first check:
 *   - update mapping through link_to_record and calling UpdateLink.
 *   - update erasing L1 and Delete-ing the configured cache on success.
 */
BOOST_AUTO_TEST_CASE(update_happy_path_invalidates_both_tiers)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("uuu")));
    auto cache = std::make_shared<RecordingCacheStore>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, cache, l1, clock);

    BOOST_REQUIRE(store.findBySlug("uuu").has_value());  // populate L1
    BOOST_REQUIRE(l1->get("uuu").has_value());

    Link updated = makeLink("uuu");
    updated.updated_at = "2026-02-02T00:00:00Z";
    updated.enabled = false;
    updated.deleted_at = "2026-02-02T00:00:00Z";
    updated.tags = {"x"};
    AppError error;
    BOOST_REQUIRE(store.update(updated, &error));

    const ::LinkRecord* record = repo->find("uuu");
    BOOST_REQUIRE(record != nullptr);
    BOOST_CHECK(!record->is_active);
    BOOST_CHECK_EQUAL(record->attributes.count(std::string(attr_deleted)), 1u);
    BOOST_CHECK_EQUAL(record->attributes.count(std::string(attr_tags)), 1u);

    BOOST_CHECK(!l1->get("uuu").has_value());  // L1 erased
    BOOST_CHECK_EQUAL(cache->delete_calls, 1);
    BOOST_CHECK_EQUAL(cache->last_deleted_key, "uuu");
}

/**
 * [Unit][App] update of a missing record maps RepoError::not_found to
 * not_found / "Link not found"; L1 is still erased.
 *
 * If this breaks, first check:
 *   - map_repo_error's not_found branch.
 *   - update erasing L1 regardless of the UpdateLink outcome.
 */
BOOST_AUTO_TEST_CASE(update_missing_record_is_not_found)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    AppError error;
    BOOST_CHECK(!store.update(makeLink("ghost"), &error));
    BOOST_CHECK(error.code == AppErrorCode::not_found);
    BOOST_CHECK_EQUAL(error.detail, "Link not found");
    BOOST_CHECK_EQUAL(repo->update_calls, 1);
    BOOST_CHECK(!l1->get("ghost").has_value());  // L1 erased
}

/**
 * [Unit][App] A backend update failure maps to storage_failure, erases
 * L1, and does not require a configured-cache Delete.
 *
 * If this breaks, first check:
 *   - update erasing L1 on a failed UpdateLink.
 *   - update skipping the configured-cache Delete on failure.
 */
BOOST_AUTO_TEST_CASE(update_backend_failure_is_storage_failure)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("uuu")));
    repo->update_error = ::RepoError::transient_failure;
    auto cache = std::make_shared<RecordingCacheStore>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, cache, l1, clock);

    BOOST_REQUIRE(store.findBySlug("uuu").has_value());  // populate L1

    AppError error;
    BOOST_CHECK(!store.update(makeLink("uuu"), &error));
    BOOST_CHECK(error.code == AppErrorCode::storage_failure);
    BOOST_CHECK_EQUAL(error.detail, "repository failure");
    BOOST_CHECK(!l1->get("uuu").has_value());  // L1 erased
    BOOST_CHECK_EQUAL(cache->delete_calls, 0);  // not required on failure
}

/**
 * [Unit][App] Both methods accept error == nullptr on a failing call
 * without dereferencing it.
 *
 * If this breaks, first check:
 *   - every error write guarded by an `error != nullptr` check.
 */
BOOST_AUTO_TEST_CASE(null_error_out_param_is_accepted)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    repo->put(makeRecord(makeLink("aaa")));
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    BOOST_CHECK_NO_THROW(BOOST_CHECK(!store.create(makeLink("aaa"), nullptr)));
    BOOST_CHECK_NO_THROW(
        BOOST_CHECK(!store.update(makeLink("ghost"), nullptr)));
}

/**
 * [Unit][App] Read-your-writes: create then findBySlug is served from L1
 * (no repository read); update then findBySlug re-reads and returns the
 * updated link (no stale L1 copy survives a write).
 *
 * If this breaks, first check:
 *   - create populating L1 and update erasing it.
 */
BOOST_AUTO_TEST_CASE(read_your_writes)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, nullptr, l1, clock);

    // create -> L1 hit on the next read.
    BOOST_REQUIRE(store.create(makeLink("aaa")));
    const auto after_create = store.findBySlug("aaa");
    BOOST_REQUIRE(after_create.has_value());
    BOOST_CHECK_EQUAL(repo->get_calls, 0);  // served from L1

    // update -> next read re-reads and sees the new target.
    Link updated = makeLink("aaa");
    updated.updated_at = "2026-02-02T00:00:00Z";
    updated.target_url = "https://example.com/CHANGED";
    BOOST_REQUIRE(store.update(updated));
    const auto after_update = store.findBySlug("aaa");
    BOOST_REQUIRE(after_update.has_value());
    BOOST_CHECK_EQUAL(after_update->target_url, "https://example.com/CHANGED");
    BOOST_CHECK_EQUAL(repo->get_calls, 1);  // L1 was erased, re-read
}

/**
 * [Unit][App] A throwing ICacheStore::Delete never escapes create or
 * update (cache failures never fail a request).
 *
 * If this breaks, first check:
 *   - best_effort_cache_delete containing the exception.
 */
BOOST_AUTO_TEST_CASE(throwing_cache_delete_does_not_escape)
{
    ManualClock clock(kT0);
    auto repo = std::make_shared<FakeMetadataRepository>();
    auto cache = std::make_shared<RecordingCacheStore>();
    cache->delete_throws = true;
    auto l1 = makeL1(clock);
    RepositoryLinkStore store(repo, cache, l1, clock);

    // create succeeds even though the best-effort cache Delete throws.
    bool created = false;
    BOOST_CHECK_NO_THROW(created = store.create(makeLink("aaa")));
    BOOST_CHECK(created);

    // update succeeds even though the best-effort cache Delete throws.
    Link updated = makeLink("aaa");
    updated.updated_at = "2026-02-02T00:00:00Z";
    bool updated_ok = false;
    BOOST_CHECK_NO_THROW(updated_ok = store.update(updated));
    BOOST_CHECK(updated_ok);
}
