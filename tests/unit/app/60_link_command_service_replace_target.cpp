/**
 * @file 60_link_command_service_replace_target.cpp
 * @brief Unit tests: ReplaceLinkTarget (the single destructive-overwrite
 * service method behind `link.import` on_conflict=overwrite) replaces only a
 * link's target through the same URL normalization / SSRF check as create,
 * preserves every other field, refuses unknown and soft-deleted slugs, maps a
 * store failure to `storage_failure`, and invalidates the cache via update.
 */
#define BOOST_TEST_MODULE LinkCommandServiceReplaceTarget
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/test/unit_test.hpp>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/types.hpp>

#include "app_test_fakes.hpp"

using namespace url_shortener;
using namespace url_shortener::app;
using namespace test_fakes;

namespace
{

/**
 * @brief In-memory `ILinkStore` that records `invalidateCache` calls and
 * mirrors the real update-invalidates-cache contract.
 *
 * Unlike the shared @ref FakeLinkStore, this store's @ref update calls
 * @ref invalidateCache(slug) just as `RepositoryLinkStore` does, so a test can
 * assert that a successful `ReplaceLinkTarget` reaches the cache invalidation
 * through `update` (the service adds no separate invalidation path).
 */
class RecordingLinkStore final : public ILinkStore
{
public:
    ///< Slugs passed to @ref invalidateCache, in call order.
    std::vector<std::string> invalidated;

    ///< When `true`, the next @ref update reports `storage_failure`.
    bool fail_update = false;

    /// Seed a link into the store directly, bypassing validation.
    void seed(const Link& link)
    {
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
    }

    bool create(const Link& link, AppError* /*error*/ = nullptr) override
    {
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
        return true;
    }

    std::optional<Link> findBySlug(const std::string& slug) const override
    {
        const auto it = by_slug_.find(slug);
        return it != by_slug_.end() ? std::optional<Link>(it->second)
                                    : std::nullopt;
    }

    std::optional<Link> findById(const std::string& id) const override
    {
        const auto it = by_id_.find(id);
        return it != by_id_.end() ? std::optional<Link>(it->second)
                                  : std::nullopt;
    }

    bool slugExists(const std::string& slug) const override
    {
        return by_slug_.count(slug) != 0;
    }

    void invalidateCache(const std::string& slug) override
    {
        invalidated.push_back(slug);
    }

    bool update(const Link& link, AppError* error = nullptr) override
    {
        if (fail_update) {
            if (error != nullptr) {
                error->code = AppErrorCode::storage_failure;
                error->detail = "forced storage failure";
            }
            return false;
        }
        by_slug_[link.slug] = link;
        by_id_[link.id] = link;
        invalidateCache(link.slug);  // mirror the ILinkStore update contract
        return true;
    }

private:
    std::unordered_map<std::string, Link> by_slug_;
    std::unordered_map<std::string, Link> by_id_;
};

/// A fully-populated, non-deleted seed link whose fields (other than target
/// and timestamps) must survive an overwrite unchanged.
Link make_seed_link()
{
    Link link;
    link.id = "id-123";
    link.slug = "keep";
    link.target_url = "https://old.example/old";
    link.created_at = "2000-01-01T00:00:00Z";
    link.updated_at = "2000-01-01T00:00:00Z";
    link.expires_at = std::string {"2099-12-31T23:59:59Z"};
    link.enabled = false;
    link.tags = {"alpha", "beta"};
    link.metadata = {{"k", "v"}};
    Link::Campaign campaign;
    campaign.name = std::string {"spring"};
    link.campaign = campaign;
    link.redirect_type = RedirectType::permanent;
    return link;
}

}  // namespace

/**
 * [Unit][App] A valid overwrite replaces the target, bumps `updated_at`, and
 * leaves every other field (id, created_at, enabled, tags, metadata, campaign,
 * expires_at, redirect_type) untouched.
 *
 * If this breaks, first check:
 *   - ReplaceLinkTarget mutates only target_url and updated_at before update.
 *   - normalizeTargetUrl produced the expected normalized URL.
 */
BOOST_AUTO_TEST_CASE(replaces_target_bumps_updated_at_other_fields_untouched)
{
    RecordingLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    store.seed(make_seed_link());
    LinkCommandService svc(store, stats, config);

    ReplaceLinkTargetCommand cmd;
    cmd.slug = "keep";
    cmd.target_url = "https://new.example/new";

    const auto result = svc.ReplaceLinkTarget(cmd);
    BOOST_REQUIRE(result.ok());
    const auto& view = *result.value;

    BOOST_CHECK_EQUAL(view.target_url, "https://new.example/new");
    BOOST_CHECK(view.updated_at != "2000-01-01T00:00:00Z");  // bumped

    // Untouched fields.
    BOOST_CHECK_EQUAL(view.id, "id-123");
    BOOST_CHECK_EQUAL(view.slug, "keep");
    BOOST_CHECK_EQUAL(view.created_at, "2000-01-01T00:00:00Z");
    BOOST_CHECK_EQUAL(view.enabled, false);
    BOOST_REQUIRE(view.expires_at.has_value());
    BOOST_CHECK_EQUAL(*view.expires_at, "2099-12-31T23:59:59Z");
    const std::vector<std::string> expected_tags {"alpha", "beta"};
    const std::unordered_map<std::string, std::string> expected_meta {
        {"k", "v"}};
    BOOST_CHECK(view.tags == expected_tags);
    BOOST_CHECK(view.metadata == expected_meta);
    BOOST_REQUIRE(view.campaign.has_value());
    BOOST_REQUIRE(view.campaign->name.has_value());
    BOOST_CHECK_EQUAL(*view.campaign->name, "spring");
    BOOST_CHECK((view.redirect_type == RedirectType::permanent));

    // The stored record reflects the same change.
    const auto stored = store.findBySlug("keep");
    BOOST_REQUIRE(stored.has_value());
    BOOST_CHECK_EQUAL(stored->target_url, "https://new.example/new");
    BOOST_CHECK_EQUAL(stored->created_at, "2000-01-01T00:00:00Z");
}

/**
 * [Unit][App] A successful overwrite invalidates the cache for the slug (via
 * the store's update contract; the service adds no second path).
 */
BOOST_AUTO_TEST_CASE(successful_overwrite_invalidates_cache)
{
    RecordingLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    store.seed(make_seed_link());
    LinkCommandService svc(store, stats, config);

    ReplaceLinkTargetCommand cmd;
    cmd.slug = "keep";
    cmd.target_url = "https://new.example/x";

    BOOST_REQUIRE(svc.ReplaceLinkTarget(cmd).ok());
    BOOST_REQUIRE(store.invalidated.size() == 1u);
    BOOST_CHECK_EQUAL(store.invalidated.front(), "keep");
}

/**
 * [Unit][App] An un-normalizable target URL fails `invalid_url` and nothing is
 * written or invalidated.
 */
BOOST_AUTO_TEST_CASE(invalid_url_rejected)
{
    RecordingLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    store.seed(make_seed_link());
    LinkCommandService svc(store, stats, config);

    ReplaceLinkTargetCommand cmd;
    cmd.slug = "keep";
    cmd.target_url = "not-a-url";

    const auto result = svc.ReplaceLinkTarget(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_url);
    BOOST_CHECK(store.invalidated.empty());
    BOOST_CHECK_EQUAL(store.findBySlug("keep")->target_url,
                      "https://old.example/old");
}

/**
 * [Unit][App] A private-host target is rejected `invalid_url` by the same SSRF
 * switch as create, but is accepted when `shortener_allow_private_targets` is
 * set, proving ReplaceLinkTarget shares the one normalization/SSRF path.
 */
BOOST_AUTO_TEST_CASE(private_host_honours_allow_private_switch)
{
    {
        RecordingLinkStore store;
        FakeLinkStatsReader stats;
        ServerConfig config = makeTestConfig();  // allow_private = false
        store.seed(make_seed_link());
        LinkCommandService svc(store, stats, config);

        ReplaceLinkTargetCommand cmd;
        cmd.slug = "keep";
        cmd.target_url = "http://127.0.0.1/";
        const auto result = svc.ReplaceLinkTarget(cmd);
        BOOST_REQUIRE(!result.ok());
        BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::invalid_url);
    }
    {
        RecordingLinkStore store;
        FakeLinkStatsReader stats;
        ServerConfig config = makeTestConfig();
        config.shortener_allow_private_targets = true;
        store.seed(make_seed_link());
        LinkCommandService svc(store, stats, config);

        ReplaceLinkTargetCommand cmd;
        cmd.slug = "keep";
        cmd.target_url = "http://127.0.0.1/";
        const auto result = svc.ReplaceLinkTarget(cmd);
        BOOST_REQUIRE(result.ok());
    }
}

/**
 * [Unit][App] An unknown slug fails `not_found`.
 */
BOOST_AUTO_TEST_CASE(unknown_slug_not_found)
{
    RecordingLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    LinkCommandService svc(store, stats, config);

    ReplaceLinkTargetCommand cmd;
    cmd.slug = "missing";
    cmd.target_url = "https://new.example/";

    const auto result = svc.ReplaceLinkTarget(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::not_found);
}

/**
 * [Unit][App] A soft-deleted slug fails `slug_conflict` (overwrite never
 * resurrects or edits a deleted record) and leaves the record byte-unchanged.
 */
BOOST_AUTO_TEST_CASE(soft_deleted_slug_conflict_record_unchanged)
{
    RecordingLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    Link deleted = make_seed_link();
    deleted.deleted_at = std::string {"2001-02-03T04:05:06Z"};
    store.seed(deleted);
    LinkCommandService svc(store, stats, config);

    ReplaceLinkTargetCommand cmd;
    cmd.slug = "keep";
    cmd.target_url = "https://new.example/";

    const auto result = svc.ReplaceLinkTarget(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::slug_conflict);

    const auto stored = store.findBySlug("keep");
    BOOST_REQUIRE(stored.has_value());
    BOOST_CHECK_EQUAL(stored->target_url, "https://old.example/old");
    BOOST_CHECK_EQUAL(stored->updated_at, "2000-01-01T00:00:00Z");
    BOOST_CHECK(store.invalidated.empty());
}

/**
 * [Unit][App] A store `update` failure surfaces as `storage_failure`.
 */
BOOST_AUTO_TEST_CASE(store_update_failure_maps_to_storage_failure)
{
    RecordingLinkStore store;
    FakeLinkStatsReader stats;
    const auto config = makeTestConfig();
    store.seed(make_seed_link());
    store.fail_update = true;
    LinkCommandService svc(store, stats, config);

    ReplaceLinkTargetCommand cmd;
    cmd.slug = "keep";
    cmd.target_url = "https://new.example/";

    const auto result = svc.ReplaceLinkTarget(cmd);
    BOOST_REQUIRE(!result.ok());
    BOOST_CHECK_EQUAL(result.error.code, AppErrorCode::storage_failure);
}
