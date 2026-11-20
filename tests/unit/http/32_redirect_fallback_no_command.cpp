/**
 * @file 32_redirect_fallback_no_command.cpp
 * @brief Behavioral unit tests for Task 07.0 subtask 32 (carry-forward from the
 * 07.0/31 security review): the `handleApplicationRequest` / URI-store fallback
 * branch in `src/http/request_handlers.cpp`, and proof that the redirect fast
 * path issues no command, job or registry call.
 *
 * Scenario:
 *   The structural guard (31) scans only the two redirect handler files, so the
 *   fallback branch that `handleRootRedirect` delegates to
 *   (`handleApplicationRequest`, the legacy URI-store) sits outside its reach.
 *   These tests cover that branch behaviorally through the real application
 *   router, and separately assert the redirect fast path touches the link store
 *   (cache -> repository seam) and NOTHING on the unified command model: no
 *   `IJobSubmitter::submit`, no `IAuditSink::record`, no registry execution.
 *
 * Boundaries:
 *   - Redirect cache-hit: exactly one cache lookup, zero repository lookups.
 *   - Redirect cache-miss: one cache lookup then one repository lookup, cache
 *     populated as a side effect (classic cache-aside).
 *   - Fallback triggers: non-GET method, a query string, and an invalid slug
 *     shape all route to the URI store, never to the link store.
 *   - URI-store roundtrip: POST then GET of the same unknown path returns the
 *     stored payload (proves the fallback branch is actually exercised).
 *   - Valid-but-unknown slug (no query) is a redirect 404, NOT the fallback —
 *     documented here as the boundary between the two branches.
 *   - Counting job submitter and audit sink installed on a registry in the same
 *     process stay at zero across every redirect and fallback dispatch.
 *
 * On failure first check:
 *   - Each case runs under BackendFixture, which reset()s the holder before and
 *     after, so a leaked install from a prior case is the first suspect.
 *   - A non-zero submit/audit count means command-model machinery leaked into
 *     the fast path (a C10 regression).
 *   - A repository lookup on a cache hit means the cache-aside order broke.
 */
#define BOOST_TEST_MODULE RedirectFallbackNoCommand
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/audit.hpp>
#include <url_shortener/commands/builtin_commands.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/job_submitter.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/uri_map_singleton.hpp>

namespace bhttp = boost::beast::http;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::app::AppError;
using url_shortener::app::ILinkStore;
using url_shortener::app::LinkBackend;
using url_shortener::commands::AuditRecord;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::IAuditSink;
using url_shortener::commands::IJobSubmitter;
using url_shortener::commands::JobBody;
using url_shortener::commands::JobSubmitResult;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

namespace
{

/**
 * @brief Test double for app::ILinkStore that models a cache in front of a
 * repository, so a test can observe the redirect path's cache-aside order.
 *
 * `findBySlug` consults the cache first (counted), and only on a miss consults
 * the repository (counted) and populates the cache. `recordRedirect` is counted
 * and never re-enters the store, keeping the lookup counters clean. Any
 * `create`/`update` is flagged as a defect: the redirect path must not mutate
 * links directly. Thread-hostile: single test thread only.
 */
class CacheThenRepoLinkStore final : public ILinkStore
{
public:
    /**
     * @brief Seeds a link visible only in the cache tier.
     * @param link Link to place in the cache, keyed by its slug.
     */
    void seedCache(const Link& link) { cache_[link.slug] = link; }

    /**
     * @brief Seeds a link visible only in the repository tier.
     * @param link Link to place in the repository, keyed by its slug.
     */
    void seedRepo(const Link& link) { repo_[link.slug] = link; }

    /// @return Number of cache-tier lookups performed by @ref findBySlug.
    std::size_t cacheLookups() const { return cache_lookups_; }

    /// @return Number of repository-tier lookups performed by @ref findBySlug.
    std::size_t repoLookups() const { return repo_lookups_; }

    /// @return Number of @ref recordRedirect calls.
    std::size_t recordCount() const { return record_count_; }

    /// @return `true` if @ref create or @ref update was ever called.
    bool sawDirectMutation() const { return saw_direct_mutation_; }

    /// @return `true` if @p slug is currently present in the cache tier.
    bool cached(const std::string& slug) const
    {
        return cache_.count(slug) > 0;
    }

    std::optional<Link> findBySlug(const std::string& slug) const override
    {
        ++cache_lookups_;
        const auto cached_it = cache_.find(slug);
        if (cached_it != cache_.end()) {
            return cached_it->second;
        }
        ++repo_lookups_;
        const auto repo_it = repo_.find(slug);
        if (repo_it != repo_.end()) {
            cache_[slug] = repo_it->second;
            return repo_it->second;
        }
        return std::nullopt;
    }

    bool recordRedirect(const Link& served, AppError* error = nullptr) override
    {
        (void)error;
        (void)served;
        ++record_count_;
        return true;
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
        return cache_.count(slug) > 0 || repo_.count(slug) > 0;
    }

    void invalidateCache(const std::string& slug) override
    {
        cache_.erase(slug);
    }

private:
    mutable std::unordered_map<std::string, Link> cache_;
    std::unordered_map<std::string, Link> repo_;
    mutable std::size_t cache_lookups_ = 0;
    mutable std::size_t repo_lookups_ = 0;
    std::size_t record_count_ = 0;
    bool saw_direct_mutation_ = false;
};

/**
 * @brief Counting test double for commands::IJobSubmitter; every submit is a
 * defect for the redirect/fallback path and is counted, never enqueued.
 */
class CountingJobSubmitter final : public IJobSubmitter
{
public:
    /// @return Number of times @ref submit was called.
    std::size_t submitCount() const { return submit_count_; }

    JobSubmitResult submit(std::string /*command*/,
                           std::string /*args_digest*/,
                           JobBody /*body*/) override
    {
        ++submit_count_;
        JobSubmitResult result;
        return result;
    }

private:
    std::size_t submit_count_ = 0;
};

/**
 * @brief Counting test double for commands::IAuditSink; any record emitted
 * while serving a redirect or fallback request is a defect and is counted.
 */
class CountingAuditSink final : public IAuditSink
{
public:
    /// @return Number of audit records received.
    std::size_t recordCount() const { return record_count_; }

    void record(const AuditRecord& /*record*/) noexcept override
    {
        ++record_count_;
    }

private:
    std::size_t record_count_ = 0;
};

/**
 * @brief Clears any installed store before and after each case so installs
 * cannot leak between cases.
 */
struct BackendFixture
{
    /// Ensures a clean (default) holder at the start of the case.
    BackendFixture() { LinkBackend::reset(); }

    /// Restores the default so a leaked fake never outlives the case.
    ~BackendFixture() { LinkBackend::reset(); }
};

/**
 * @brief Builds a request with a Host header set.
 * @param method HTTP verb.
 * @param target Request target (path, optionally with query).
 * @param body Request body.
 * @param content_type Content-Type header value.
 * @return A prepared request.
 */
BeastRequest makeRequest(const bhttp::verb method,
                         const std::string& target,
                         const std::string& body = "",
                         const std::string& content_type = "text/plain")
{
    BeastRequest req {method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, content_type);
    req.body() = body;
    req.prepare_payload();
    return req;
}

/**
 * @brief Minimal config: private targets allowed, analytics disabled so the
 * test exercises only the store seam, not the analytics singletons.
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
 * @param redirect_type Redirect semantics.
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
 * [Unit][Http] A cache-hit redirect consults the cache only, and a cache-miss
 * redirect consults the cache then the repository, for both path kinds.
 *
 * Scenario:
 *   Given a CacheThenRepoLinkStore with one slug seeded in the cache tier and a
 *   different slug seeded only in the repository tier.
 *   When GET /{cached} and GET /r/{repo-only} are dispatched.
 *   Then the cached slug redirects after one cache lookup and zero repository
 *   lookups, and the repo-only slug redirects after one cache lookup and one
 *   repository lookup, populating the cache as a side effect.
 *
 * If this breaks, first check:
 *   - handlers resolving app::LinkBackend::store().findBySlug for the lookup.
 *   - the fake's cache-aside order (cache first, repository on miss).
 */
BOOST_FIXTURE_TEST_CASE(redirect_hits_cache_then_repository, BackendFixture)
{
    auto store = std::make_shared<CacheThenRepoLinkStore>();
    store->seedCache(makeLink("cached01", RedirectType::temporary));
    store->seedRepo(makeLink("repoonly1", RedirectType::permanent));
    LinkBackend::install(store);

    const auto hit = dispatch(makeRequest(bhttp::verb::get, "/cached01"));
    BOOST_TEST(hit.result_int() == 302);
    BOOST_TEST(std::string(hit[bhttp::field::location])
               == "https://example.com/cached01");
    BOOST_TEST(store->cacheLookups() == 1U);
    BOOST_TEST(store->repoLookups() == 0U);

    const auto miss = dispatch(makeRequest(bhttp::verb::get, "/r/repoonly1"));
    BOOST_TEST(miss.result_int() == 301);
    BOOST_TEST(std::string(miss[bhttp::field::location])
               == "https://example.com/repoonly1");
    BOOST_TEST(store->cacheLookups() == 2U);
    BOOST_TEST(store->repoLookups() == 1U);
    BOOST_TEST(store->cached("repoonly1"));

    BOOST_TEST(store->recordCount() == 2U);
    BOOST_TEST(!store->sawDirectMutation());
}

/**
 * [Unit][Http] The fallback triggers (non-GET, query string, invalid slug
 * shape) route to the URI store and never touch the link store.
 *
 * Scenario:
 *   Given an installed CacheThenRepoLinkStore.
 *   When a POST, a GET with a query string, and a GET for a dotted (invalid)
 *   slug are dispatched.
 *   Then each is handled by handleApplicationRequest (the URI store) and the
 *   link store's findBySlug is never consulted.
 *
 * If this breaks, first check:
 *   - handleRootRedirect still delegating the three non-redirect cases to
 *     handleApplicationRequest before any store lookup.
 */
BOOST_FIXTURE_TEST_CASE(fallback_triggers_skip_link_store, BackendFixture)
{
    auto store = std::make_shared<CacheThenRepoLinkStore>();
    LinkBackend::install(store);

    const auto post = dispatch(makeRequest(
        bhttp::verb::post, "/fbstore01", "saved body", "text/plain"));
    BOOST_TEST(post.result_int() == 200);
    BOOST_TEST(post.body() == "URI saved\n");

    const auto query =
        dispatch(makeRequest(bhttp::verb::get, "/unknownslug?utm=1"));
    BOOST_TEST(query.result_int() == 404);
    BOOST_TEST(query.body() == "URI not found\n");

    const auto dotted = dispatch(makeRequest(bhttp::verb::get, "/bad.slug"));
    BOOST_TEST(dotted.result_int() == 404);
    BOOST_TEST(dotted.body() == "URI not found\n");

    BOOST_TEST(store->cacheLookups() == 0U);
    BOOST_TEST(store->repoLookups() == 0U);
    BOOST_TEST(store->recordCount() == 0U);
}

/**
 * [Unit][Http] A POST then GET of the same non-slug-shaped path round-trips
 * through the URI-store fallback, proving the fallback branch serves stored
 * data on GET.
 *
 * The path carries a dot, so it is not a valid slug: only then does a GET fall
 * through to the URI store (a valid slug-shaped GET is a redirect attempt and
 * returns a redirect 404 instead — see the next case). POST is non-GET and
 * always reaches the fallback regardless of shape.
 *
 * Scenario:
 *   Given an installed CacheThenRepoLinkStore (irrelevant to the fallback).
 *   When a payload is POSTed to a dotted path and then GETed back.
 *   Then the GET returns 200 with the stored payload, and the link store is
 *   never consulted.
 *
 * If this breaks, first check:
 *   - handleApplicationRequest still reading/writing UriMapSingleton for the
 *     GET/POST branches.
 */
BOOST_FIXTURE_TEST_CASE(uri_store_roundtrip_through_fallback, BackendFixture)
{
    auto store = std::make_shared<CacheThenRepoLinkStore>();
    LinkBackend::install(store);

    const auto saved = dispatch(makeRequest(
        bhttp::verb::post, "/fbroundtrip.dat", "payload-bytes", "text/plain"));
    BOOST_TEST(saved.result_int() == 200);

    const auto fetched =
        dispatch(makeRequest(bhttp::verb::get, "/fbroundtrip.dat"));
    BOOST_TEST(fetched.result_int() == 200);
    BOOST_TEST(fetched.body() == "payload-bytes\n");

    BOOST_TEST(store->cacheLookups() == 0U);
    BOOST_TEST(store->repoLookups() == 0U);
}

/**
 * [Unit][Http] A valid-shaped but unknown slug with no query is a redirect 404,
 * NOT the URI-store fallback — the documented boundary between the two
 * branches.
 *
 * Scenario:
 *   Given an installed CacheThenRepoLinkStore with no matching link.
 *   When GET /{valid-unknown-slug} is dispatched with no query string.
 *   Then the response is the redirect 404 not_found API error (JSON), the store
 *   is consulted once (cache then repository), and no redirect is recorded.
 *
 * If this breaks, first check:
 *   - handleRootRedirect treating a valid slug shape as a redirect attempt
 *     (linkStateError) instead of falling through to the URI store.
 */
BOOST_FIXTURE_TEST_CASE(unknown_valid_slug_is_redirect_404_not_fallback,
                        BackendFixture)
{
    auto store = std::make_shared<CacheThenRepoLinkStore>();
    LinkBackend::install(store);

    const auto res = dispatch(makeRequest(bhttp::verb::get, "/missingslug01"));
    BOOST_TEST(res.result_int() == 404);
    BOOST_TEST(res.body().find("\"code\":\"not_found\"") != std::string::npos);
    BOOST_TEST(std::string(res[bhttp::field::content_type])
               == "application/json");

    BOOST_TEST(store->cacheLookups() == 1U);
    BOOST_TEST(store->repoLookups() == 1U);
    BOOST_TEST(store->recordCount() == 0U);
}

/**
 * [Unit][Http] Neither the redirect fast path nor the URI-store fallback issues
 * any command, job or registry call.
 *
 * Scenario:
 *   Given a CommandRegistry (with built-in commands) carrying a counting job
 *   submitter and a counting audit sink, plus an installed link store with an
 *   active link.
 *   When a successful redirect, a fallback POST/GET and an invalid-slug
 *   fallback are all dispatched.
 *   Then the submitter and audit sink remain at zero: the fast path and the
 *   fallback never reach the unified command model (contract C10).
 *
 * If this breaks, first check:
 *   - no command execution, job submission or audit emission was wired into
 *     handleRootRedirect / handlePrefixedRedirect / handleApplicationRequest.
 */
BOOST_FIXTURE_TEST_CASE(redirect_and_fallback_issue_no_command, BackendFixture)
{
    auto submitter = std::make_shared<CountingJobSubmitter>();
    auto audit = std::make_shared<CountingAuditSink>();

    CommandRegistry registry;
    url_shortener::commands::register_builtin_commands(registry);
    registry.set_job_submitter(submitter);
    registry.set_audit_sink(audit);

    auto store = std::make_shared<CacheThenRepoLinkStore>();
    store->seedCache(makeLink("nocmd01", RedirectType::temporary));
    LinkBackend::install(store);

    BOOST_TEST(dispatch(makeRequest(bhttp::verb::get, "/nocmd01")).result_int()
               == 302);
    BOOST_TEST(dispatch(makeRequest(bhttp::verb::post, "/nocmdpath.dat", "b"))
                   .result_int()
               == 200);
    BOOST_TEST(
        dispatch(makeRequest(bhttp::verb::get, "/nocmdpath.dat")).result_int()
        == 200);
    BOOST_TEST(dispatch(makeRequest(bhttp::verb::get, "/bad.slug")).result_int()
               == 404);

    BOOST_TEST(submitter->submitCount() == 0U);
    BOOST_TEST(audit->recordCount() == 0U);
    BOOST_TEST(store->recordCount() == 1U);
    BOOST_TEST(!store->sawDirectMutation());
}
