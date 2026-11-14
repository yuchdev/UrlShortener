/**
 * @file 30_redirect_through_backend.cpp
 * @brief Unit tests for subtask 02.0/08: the redirect fast path obtains its
 * Link and records its statistics through app::LinkBackend::store() instead
 * of the legacy free functions.
 *
 * Scenario:
 *   GET /{slug} and GET /r/{slug} must answer their two storage questions
 *   (findBySlug, recordRedirect) through the installed ILinkStore. With a
 *   RecordingLinkStore installed, these tests prove the handlers route
 *   lookups and stat records through the store, and that recordRedirect is
 *   invoked only on a successful (active-link) redirect — never on a 404,
 *   410, or policy outcome (threat model 02.0/08). The handlers must never
 *   call create()/update() directly: that increment moved into
 *   ILinkStore::recordRedirect (02.0/05), so those are treated as defects.
 *
 * Boundaries:
 *   - Active link only in the fake (legacy repository empty): one findBySlug,
 *     one recordRedirect carrying the served slug, configured 301/302.
 *   - Missing slug: 404, recordRedirect 0 calls.
 *   - Disabled (410)/deleted (404 link_deleted)/expired (410): error as
 *     today, recordRedirect 0 calls.
 *   - Invalid slug shape: findBySlug not called.
 *   - recordRedirect returning false does not change the response (best
 *     effort).
 *   Legacy-default parity (no install) is covered by 12_* / 13_* and the
 *   persisted-counter check by 07_stats_increment_test.
 *
 * On failure first check:
 *   - Each case runs under BackendFixture, which reset()s the holder before
 *     and after, so a leaked install from a prior case is the first suspect.
 *   - recordRedirect being called on a non-success outcome means a guard
 *     moved below incrementRedirectStats in the handler.
 *   - create()/update() being flagged means the handler stopped going
 *     through recordRedirect for the increment.
 */
#define BOOST_TEST_MODULE RedirectThroughBackend
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/http/router_builder.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::app::AppError;
using url_shortener::app::ILinkStore;
using url_shortener::app::LinkBackend;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

namespace
{

/**
 * @brief Test double for app::ILinkStore that records how the redirect
 * handlers use the store without touching any global singleton.
 *
 * Holds a slug-keyed map seeded by the test, counts @ref findBySlug and
 * @ref recordRedirect calls, remembers the slug last recorded, and flags
 * any @ref create / @ref update call as a defect (the redirect path must
 * never mutate links directly — the increment lives in recordRedirect).
 * Thread-hostile: use from a single test thread only.
 */
class RecordingLinkStore final : public ILinkStore
{
public:
    /**
     * @brief Seeds a link to be returned by @ref findBySlug.
     * @param link Link to store, keyed by its slug.
     */
    void seed(const Link& link) { by_slug_[link.slug] = link; }

    /**
     * @brief Makes the next @ref recordRedirect calls return @p value.
     * @param value Value every subsequent recordRedirect returns.
     */
    void setRecordResult(bool value) { record_result_ = value; }

    /// @return Number of times @ref findBySlug was called.
    std::size_t findCount() const { return find_count_; }

    /// @return Number of times @ref recordRedirect was called.
    std::size_t recordCount() const { return record_count_; }

    /// @return Slug of the link passed to the most recent @ref
    /// recordRedirect, or empty if it was never called.
    const std::string& lastRecordedSlug() const { return last_recorded_slug_; }

    /// @return `true` if @ref create or @ref update was ever called (a
    /// defect for the redirect path).
    bool sawDirectMutation() const { return saw_direct_mutation_; }

    std::optional<Link> findBySlug(const std::string& slug) const override
    {
        ++find_count_;
        const auto it = by_slug_.find(slug);
        if (it != by_slug_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool recordRedirect(const Link& served, AppError* error = nullptr) override
    {
        (void)error;
        ++record_count_;
        last_recorded_slug_ = served.slug;
        return record_result_;
    }

    bool create(const Link& /*link*/, AppError* error = nullptr) override
    {
        (void)error;
        saw_direct_mutation_ = true;
        return false;
    }

    bool update(const Link& /*link*/, AppError* error = nullptr) override
    {
        (void)error;
        saw_direct_mutation_ = true;
        return false;
    }

    std::optional<Link> findById(const std::string& /*id*/) const override
    {
        return std::nullopt;
    }

    bool slugExists(const std::string& slug) const override
    {
        return by_slug_.count(slug) > 0;
    }

    void invalidateCache(const std::string& /*slug*/) override {}

private:
    std::unordered_map<std::string, Link> by_slug_;
    bool record_result_ = true;
    mutable std::size_t find_count_ = 0;
    std::size_t record_count_ = 0;
    std::string last_recorded_slug_;
    bool saw_direct_mutation_ = false;
};

/**
 * @brief Clears any installed store before and after each case so cases
 * cannot leak an installation into one another.
 */
struct BackendFixture
{
    /// Ensures a clean (default) holder at the start of the case.
    BackendFixture() { LinkBackend::reset(); }

    /// Restores the default so a leaked fake never outlives the case.
    ~BackendFixture() { LinkBackend::reset(); }
};

/**
 * @brief Builds a GET request for @p target with a Host header set.
 * @param target Request target (path, optionally with query).
 * @return A prepared GET request.
 */
BeastRequest makeGet(const std::string& target)
{
    BeastRequest req {bhttp::verb::get, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.prepare_payload();
    return req;
}

/**
 * @brief Minimal config with analytics disabled so the test exercises only
 * the store substitution, not the analytics singletons.
 * @return A ServerConfig for dispatch.
 */
ServerConfig testConfig()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.analytics_enabled = false;
    return config;
}

/**
 * @brief Builds an active link for @p slug.
 * @param slug Slug for the link.
 * @param redirect_type Redirect semantics (temporary/permanent).
 * @return A populated active link.
 */
Link makeLink(const std::string& slug,
              const RedirectType redirect_type = RedirectType::temporary)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = url_shortener::currentTimestamp();
    link.updated_at = link.created_at;
    link.enabled = true;
    link.redirect_type = redirect_type;
    return link;
}

/**
 * @brief Dispatches @p req through a freshly built application router.
 * @param req Request to dispatch.
 * @return The response produced by the router.
 */
boost::beast::http::response<boost::beast::http::string_body> dispatch(
    const BeastRequest& req)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, testConfig(), false);
}

}  // namespace

/**
 * [Unit][Http] An active link present only in the installed store serves the
 * configured redirect, through exactly one findBySlug and one recordRedirect.
 *
 * Scenario:
 *   Given a RecordingLinkStore holding one temporary and one permanent active
 *   link (the legacy repository is untouched).
 *   When GET /{slug} and GET /r/{slug} are dispatched.
 *   Then each returns the configured 302/301 with Location, findBySlug is
 *   called once per request, recordRedirect once per request carrying the
 *   served slug, and no direct create/update occurs.
 *
 * If this breaks, first check:
 *   - handlers resolving app::LinkBackend::store().findBySlug (not the legacy
 *     getLinkForRead).
 *   - incrementRedirectStats delegating to store().recordRedirect.
 */
BOOST_FIXTURE_TEST_CASE(active_link_serves_through_backend, BackendFixture)
{
    auto store = std::make_shared<RecordingLinkStore>();
    store->seed(makeLink("btemp01", RedirectType::temporary));
    store->seed(makeLink("bperm01", RedirectType::permanent));
    LinkBackend::install(store);

    const auto root = dispatch(makeGet("/btemp01"));
    BOOST_TEST(root.result_int() == 302);
    BOOST_TEST(std::string(root[bhttp::field::location])
               == "https://example.com/btemp01");
    BOOST_TEST(!root.keep_alive());

    const auto prefixed = dispatch(makeGet("/r/bperm01"));
    BOOST_TEST(prefixed.result_int() == 301);
    BOOST_TEST(std::string(prefixed[bhttp::field::location])
               == "https://example.com/bperm01");

    BOOST_TEST(store->findCount() == 2U);
    BOOST_TEST(store->recordCount() == 2U);
    BOOST_TEST(store->lastRecordedSlug() == "bperm01");
    BOOST_TEST(!store->sawDirectMutation());
}

/**
 * [Unit][Http] A missing slug returns 404 and never records a redirect.
 *
 * Scenario:
 *   Given an empty RecordingLinkStore.
 *   When GET /{slug} and GET /r/{slug} are dispatched for an unknown slug.
 *   Then both return 404 not_found, findBySlug is consulted, and
 *   recordRedirect is never called.
 *
 * If this breaks, first check:
 *   - recordRedirect being reached before the active-status guard.
 */
BOOST_FIXTURE_TEST_CASE(missing_slug_does_not_record, BackendFixture)
{
    auto store = std::make_shared<RecordingLinkStore>();
    LinkBackend::install(store);

    BOOST_TEST(dispatch(makeGet("/bmissing01")).result_int() == 404);
    BOOST_TEST(dispatch(makeGet("/r/bmissing01")).result_int() == 404);

    BOOST_TEST(store->findCount() == 2U);
    BOOST_TEST(store->recordCount() == 0U);
    BOOST_TEST(!store->sawDirectMutation());
}

/**
 * [Unit][Http] Terminal link states (deleted/disabled/expired) return the
 * same error responses as today and never record a redirect.
 *
 * Scenario:
 *   Given a RecordingLinkStore holding a soft-deleted, a disabled, and an
 *   expired link.
 *   When GET /r/{slug} is dispatched for each.
 *   Then the responses are 404 link_deleted, 410 link_disabled, and 410
 *   link_expired, and recordRedirect is never called.
 *
 * If this breaks, first check:
 *   - the active-status guard still preceding incrementRedirectStats.
 */
BOOST_FIXTURE_TEST_CASE(terminal_states_do_not_record, BackendFixture)
{
    auto store = std::make_shared<RecordingLinkStore>();

    Link deleted = makeLink("bdeleted01");
    deleted.deleted_at = url_shortener::currentTimestamp();
    store->seed(deleted);

    Link disabled = makeLink("bdisabled01");
    disabled.enabled = false;
    store->seed(disabled);

    Link expired = makeLink("bexpired01");
    expired.expires_at = "2000-01-01T00:00:00Z";
    store->seed(expired);

    LinkBackend::install(store);

    const auto deleted_res = dispatch(makeGet("/r/bdeleted01"));
    BOOST_TEST(deleted_res.result_int() == 404);
    BOOST_TEST(deleted_res.body().find("\"code\":\"link_deleted\"")
               != std::string::npos);

    const auto disabled_res = dispatch(makeGet("/r/bdisabled01"));
    BOOST_TEST(disabled_res.result_int() == 410);
    BOOST_TEST(disabled_res.body().find("\"code\":\"link_disabled\"")
               != std::string::npos);

    const auto expired_res = dispatch(makeGet("/r/bexpired01"));
    BOOST_TEST(expired_res.result_int() == 410);
    BOOST_TEST(expired_res.body().find("\"code\":\"link_expired\"")
               != std::string::npos);

    BOOST_TEST(store->recordCount() == 0U);
    BOOST_TEST(!store->sawDirectMutation());
}

/**
 * [Unit][Http] An invalid slug shape is rejected before the store is
 * consulted.
 *
 * Scenario:
 *   Given an installed RecordingLinkStore.
 *   When GET /r/{slug} is dispatched with a slug containing a dot.
 *   Then the response is 404 and findBySlug is never called.
 *
 * If this breaks, first check:
 *   - isValidSlug still guarding the store lookup in handlePrefixedRedirect.
 */
BOOST_FIXTURE_TEST_CASE(invalid_slug_skips_store, BackendFixture)
{
    auto store = std::make_shared<RecordingLinkStore>();
    LinkBackend::install(store);

    BOOST_TEST(dispatch(makeGet("/r/bad.slug")).result_int() == 404);
    BOOST_TEST(store->findCount() == 0U);
    BOOST_TEST(store->recordCount() == 0U);
}

/**
 * [Unit][Http] A recordRedirect that returns false does not change the
 * redirect response (best-effort stat record, C10).
 *
 * Scenario:
 *   Given a RecordingLinkStore seeded with an active link whose
 *   recordRedirect is forced to return false.
 *   When GET /{slug} is dispatched.
 *   Then the response is still the configured 302 with Location, and
 *   recordRedirect was called once.
 *
 * If this breaks, first check:
 *   - incrementRedirectStats ignoring the recordRedirect return value.
 */
BOOST_FIXTURE_TEST_CASE(failed_record_keeps_redirect, BackendFixture)
{
    auto store = std::make_shared<RecordingLinkStore>();
    store->seed(makeLink("bbesteffort01", RedirectType::temporary));
    store->setRecordResult(false);
    LinkBackend::install(store);

    const auto res = dispatch(makeGet("/bbesteffort01"));
    BOOST_TEST(res.result_int() == 302);
    BOOST_TEST(std::string(res[bhttp::field::location])
               == "https://example.com/bbesteffort01");
    BOOST_TEST(store->recordCount() == 1U);
    BOOST_TEST(!store->sawDirectMutation());
}
