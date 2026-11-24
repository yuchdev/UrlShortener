/**
 * @file 24_link_backend_holder.cpp
 * @brief Unit tests for app::LinkBackend (subtask 07): the process-wide
 * holder that returns the installed ILinkStore or the LegacyLinkStore
 * default.
 *
 * Scenario:
 *   LinkBackend is the single seam through which redirect/REST/CLI obtain
 *   "the link store". These tests prove: the default path routes through
 *   the legacy linkRepository() singleton; install() swaps the active
 *   store (reached through BuildLegacyLinkCommandService) and installed()
 *   reflects it; reset() restores the default; install-twice keeps the
 *   last; the holder keeps an installed store alive even after every
 *   external owner is dropped (threat model T-39 ownership); store() is
 *   lock-free and null-safe under a reader/writer race; and the bundle's
 *   store pointer is non-owning.
 *
 * Boundaries:
 *   - Default (nothing installed): installed()==false, store().create is
 *     visible through linkRepository().
 *   - install(fake): installed()==true, service built over the holder
 *     reaches the fake and not the legacy repository.
 *   - reset()/install-twice idempotency and last-wins.
 *   - Owner lifetime: holder holds the only strong reference.
 *   - Concurrency smoke: 4 readers vs alternating install/reset.
 *   - Bundle store pointer is non-owning (empty control block).
 *
 * On failure first check:
 *   - Each case runs under HolderFixture, which reset()s before and after,
 *     so a leaked install from a prior case is the first suspect.
 *   - Default tests use unique slugs because they share the process-wide
 *     linkRepository() singleton; a reused slug would collide.
 */
#define BOOST_TEST_MODULE LinkBackendHolder
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/storage/link_repository.hpp>

#include "app_test_fakes.hpp"

using test_fakes::FakeLinkStore;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::app::ILinkStore;
using url_shortener::app::LinkBackend;

namespace
{

/**
 * @brief Test fixture that clears any installed store before and after
 * each case so cases cannot leak an installation into one another.
 */
struct HolderFixture
{
    /// Ensures a clean (default) holder at the start of the case.
    HolderFixture() { LinkBackend::reset(); }

    /// Restores the default so a leaked fake never outlives the case.
    ~HolderFixture() { LinkBackend::reset(); }
};

/**
 * @brief Builds a minimal valid link with the given slug.
 * @param slug Slug for the link; id is derived as `"id-" + slug`.
 * @return A populated link suitable for repository round-tripping.
 */
Link makeLink(const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = "2026-01-01T00:00:00Z";
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    return link;
}

}  // namespace

/**
 * [Unit][App] With nothing installed the default store writes through to
 * the global linkRepository() singleton.
 *
 * Scenario:
 *   Given a freshly reset holder.
 *   When a link is created through store() and then read back from
 *   linkRepository().
 *   Then installed() is false and the link is visible in the singleton.
 *
 * If this breaks, first check:
 *   - store()'s default branch returning a LegacyLinkStore.
 *   - LegacyLinkStore delegating create() to linkRepository().
 */
BOOST_FIXTURE_TEST_CASE(default_store_writes_through_to_legacy_singleton,
                        HolderFixture)
{
    BOOST_CHECK(!LinkBackend::installed());

    const std::string slug = "backend-default-xyz";
    BOOST_REQUIRE(LinkBackend::store().create(makeLink(slug)));

    const auto found = url_shortener::linkRepository().getBySlug(slug);
    BOOST_REQUIRE(found.has_value());
    BOOST_CHECK_EQUAL(found->slug, slug);
}

/**
 * [Unit][App] install() swaps the active store; a service built over the
 * holder reaches the fake and not the legacy repository.
 *
 * Scenario:
 *   Given a FakeLinkStore installed.
 *   When a link is created via BuildLegacyLinkCommandService(config).
 *   Then installed() is true, store() returns the fake, the fake holds the
 *   slug, and the legacy repository does not.
 *
 * If this breaks, first check:
 *   - install() publishing the fake pointer.
 *   - BuildLegacyLinkCommandService binding to LinkBackend::store().
 */
BOOST_FIXTURE_TEST_CASE(install_routes_service_to_fake, HolderFixture)
{
    auto fake = std::make_shared<FakeLinkStore>();
    LinkBackend::install(fake);

    BOOST_CHECK(LinkBackend::installed());
    BOOST_CHECK_EQUAL(&LinkBackend::store(), fake.get());

    const auto config = test_fakes::makeTestConfig();
    auto bundle = url_shortener::app::BuildLegacyLinkCommandService(config);

    url_shortener::app::CreateLinkCommand command;
    command.target_url = "https://example.com/installed";
    command.slug = std::string("backend-installed-xyz");
    const auto result = bundle.service->CreateLink(command);
    BOOST_REQUIRE(result.value.has_value());

    BOOST_CHECK(fake->findBySlug("backend-installed-xyz").has_value());
    BOOST_CHECK(
        !url_shortener::linkRepository().getBySlug("backend-installed-xyz"));
}

/**
 * [Unit][App] reset() restores the default and install() twice keeps the
 * last store.
 *
 * Scenario:
 *   Given two fakes installed in sequence.
 *   When store() is queried, then reset() is called.
 *   Then store() returns the second fake before reset and installed() is
 *   false after reset.
 *
 * If this breaks, first check:
 *   - install() overwriting the owner (last-wins).
 *   - reset() clearing the published pointer.
 */
BOOST_FIXTURE_TEST_CASE(install_twice_last_wins_then_reset, HolderFixture)
{
    auto first = std::make_shared<FakeLinkStore>();
    auto second = std::make_shared<FakeLinkStore>();

    LinkBackend::install(first);
    LinkBackend::install(second);
    BOOST_CHECK_EQUAL(&LinkBackend::store(), second.get());

    LinkBackend::reset();
    BOOST_CHECK(!LinkBackend::installed());
}

/**
 * [Unit][App] The holder keeps an installed store alive after every
 * external owner is dropped, and reset() releases it (threat model T-39).
 *
 * Scenario:
 *   Given a fake whose only external strong reference is dropped right
 *   after install.
 *   When the weak_ptr is checked before and after reset().
 *   Then it is alive while installed and expires on reset().
 *
 * If this breaks, first check:
 *   - install() taking shared ownership (s_owner).
 *   - reset() releasing s_owner.
 */
BOOST_FIXTURE_TEST_CASE(holder_owns_store_until_reset, HolderFixture)
{
    std::weak_ptr<ILinkStore> weak;
    {
        auto fake = std::make_shared<FakeLinkStore>();
        weak = fake;
        LinkBackend::install(std::move(fake));
    }

    BOOST_CHECK(LinkBackend::installed());
    BOOST_CHECK(!weak.expired());

    LinkBackend::reset();
    BOOST_CHECK(weak.expired());
}

/**
 * [Unit][App] store() is lock-free and never null while another thread
 * alternates install()/reset() (concurrency smoke, threat model T-39).
 *
 * Scenario:
 *   Given 4 reader threads repeatedly resolving store().
 *   When one writer alternates install()/reset() 1000 times.
 *   Then no reader observes a null pointer and nothing crashes.
 *
 * If this breaks, first check:
 *   - store()'s acquire load matching install()'s release store.
 *   - reset() never leaving s_current pointing at freed memory the reader
 *     dereferences (readers only resolve the reference, they do not call
 *     virtuals on a store being torn down).
 */
BOOST_FIXTURE_TEST_CASE(store_resolution_is_null_safe_under_race, HolderFixture)
{
    std::atomic<bool> stop {false};
    std::atomic<bool> saw_null {false};

    std::vector<std::thread> readers;
    for (int i = 0; i < 4; ++i) {
        readers.emplace_back(
            [&]
            {
                while (!stop.load(std::memory_order_relaxed)) {
                    ILinkStore* const resolved = &LinkBackend::store();
                    if (resolved == nullptr) {
                        saw_null.store(true, std::memory_order_relaxed);
                    }
                }
            });
    }

    std::thread writer(
        [&]
        {
            for (int i = 0; i < 1000; ++i) {
                if ((i & 1) == 0) {
                    LinkBackend::install(std::make_shared<FakeLinkStore>());
                }
                else {
                    LinkBackend::reset();
                }
            }
        });

    writer.join();
    stop.store(true, std::memory_order_relaxed);
    for (auto& reader : readers) {
        reader.join();
    }

    BOOST_CHECK(!saw_null.load(std::memory_order_relaxed));
}

/**
 * [Unit][App] The bundle's store pointer is non-owning: copying and
 * destroying bundles never destroys the installed store.
 *
 * Scenario:
 *   Given a fake installed with no other external owner.
 *   When bundles are built, copied, and destroyed.
 *   Then the bundle store's use_count is 0 (empty control block) and the
 *   store survives until reset().
 *
 * If this breaks, first check:
 *   - BuildLegacyLinkCommandService using the aliasing shared_ptr
 *     constructor with an empty owner.
 */
BOOST_FIXTURE_TEST_CASE(bundle_store_pointer_is_non_owning, HolderFixture)
{
    std::weak_ptr<ILinkStore> weak;
    {
        auto fake = std::make_shared<FakeLinkStore>();
        weak = fake;
        LinkBackend::install(std::move(fake));
    }
    BOOST_REQUIRE(!weak.expired());

    const auto config = test_fakes::makeTestConfig();
    {
        auto bundle = url_shortener::app::BuildLegacyLinkCommandService(config);
        auto copy = bundle;
        BOOST_CHECK_EQUAL(bundle.store.use_count(), 0);
        BOOST_CHECK_EQUAL(copy.store.get(), &LinkBackend::store());
    }

    BOOST_CHECK(!weak.expired());

    LinkBackend::reset();
    BOOST_CHECK(weak.expired());
}
