/** @file 17_rest_create_via_registry.cpp
 *  @brief Characterizes POST /api/v1/links (and the compat
 *  POST /api/v1/short-urls) after create is re-pointed at the command registry
 *  (milestone 0006, Task 04.0 subtask 04), and pins the always-on create
 *  throttle (threat model T-75) plus the behavior-neutral RouteContext::peer
 *  plumbing this subtask delivers.
 *
 *  Scope: the create success/error contract (bodies, status, per-field 400
 *  codes and their ORDER) must stay byte-for-byte what it is today (contract
 *  C3); the only deliberate addition is the new 429 `rate_limited` response
 * when a per-peer create limit is exceeded. The redirect fast path is untouched
 *  (contract C10); this file only exercises the create management plane.
 */
#define BOOST_TEST_MODULE RestCreateViaRegistry
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/ip/address.hpp>
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/config/storage_config.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/http/router.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/i_rate_limiter.hpp>
#include <url_shortener/storage/inmemory/in_memory_rate_limiter.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace bhttp = boost::beast::http;
namespace composition = url_shortener::composition;
using url_shortener::http::BeastRequest;
using url_shortener::http::BeastResponse;
using url_shortener::http::RouteContext;
using url_shortener::http::Router;
using url_shortener::http::RouterBuilder;

namespace
{

/// Builds a POST (or arbitrary-method) request with a JSON body, mirroring the
/// request shape the production session produces.
BeastRequest make_request(const bhttp::verb method,
                          const std::string& target,
                          const std::string& body = "{}")
{
    BeastRequest req {method, target, 11};
    req.set(bhttp::field::host, "short.test");
    req.set(bhttp::field::content_type, "application/json");
    req.body() = body;
    req.prepare_payload();
    return req;
}

/// The permissive config the create tests dispatch with (private targets
/// allowed so example.com-style public URLs and the one explicit private-target
/// case are both exercised deterministically).
ServerConfig test_config()
{
    ServerConfig config;
    config.shortener_allow_private_targets = true;
    config.shortener_generated_slug_length = 8;
    return config;
}

/// Dispatches a request through the real application router (the production
/// seam), optionally carrying a peer address for the throttle key.
BeastResponse dispatch(
    const BeastRequest& req,
    const ServerConfig& config,
    std::optional<boost::asio::ip::address> peer = std::nullopt)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, config, false, std::move(peer));
}

/// Extracts the `"code":"..."` token from a REST error body, or "" when absent.
std::string extract_code(const std::string& body)
{
    const std::string key = "\"code\":\"";
    const auto pos = body.find(key);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + key.size();
    const auto end = body.find('"', start);
    return end == std::string::npos ? std::string {}
                                    : body.substr(start, end - start);
}

/// Installs a process-wide command runtime (built from the fixture config) with
/// an explicit create limiter, so the handler's throttle uses a limiter the
/// test controls. Resets both the runtime and the link backend on teardown so
/// no other case in the module is affected.
struct ScopedCreateRuntime
{
    ServerConfig config;

    ScopedCreateRuntime(ServerConfig cfg,
                        std::shared_ptr<::IRateLimiter> limiter)
        : config(std::move(cfg))
    {
        url_shortener::app::LinkBackend::reset();
        composition::reset_process_command_runtime();
        auto runtime = std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(
                config, url_shortener::app::LinkBackendHandle {}));
        runtime->create_rate_limiter = std::move(limiter);
        composition::install_process_command_runtime(std::move(runtime));
    }

    ~ScopedCreateRuntime()
    {
        composition::reset_process_command_runtime();
        url_shortener::app::LinkBackend::reset();
    }

    ScopedCreateRuntime(const ScopedCreateRuntime&) = delete;
    ScopedCreateRuntime& operator=(const ScopedCreateRuntime&) = delete;
};

/// A limiter that always reports a backend failure, to prove the fail-open
/// policy (an `error`-set deny lets the request through).
class FailingRateLimiter final : public IRateLimiter
{
public:
    RateLimitDecision Allow(const std::string&,
                            std::uint64_t,
                            std::chrono::seconds,
                            RateLimitError* error) override
    {
        if (error) {
            *error = RateLimitError::unavailable;
        }
        return RateLimitDecision {false, 0, {}};
    }
};

/// A limiter that records the key it was called with and always permits, to
/// prove the throttle keys on the peer address, never a header.
class CapturingRateLimiter final : public IRateLimiter
{
public:
    std::string last_key;
    int calls = 0;

    RateLimitDecision Allow(const std::string& key,
                            std::uint64_t,
                            std::chrono::seconds,
                            RateLimitError* error) override
    {
        last_key = key;
        ++calls;
        if (error) {
            *error = RateLimitError::none;
        }
        return RateLimitDecision {true, 0, {}};
    }
};

/// A limiter that always denies without an error, to prove the disable switch
/// (`link_create_rate_limit == 0`) never calls it.
class AlwaysDenyRateLimiter final : public IRateLimiter
{
public:
    int calls = 0;

    RateLimitDecision Allow(const std::string&,
                            std::uint64_t,
                            std::chrono::seconds,
                            RateLimitError* error) override
    {
        ++calls;
        if (error) {
            *error = RateLimitError::none;
        }
        return RateLimitDecision {false, 0, {}};
    }
};

boost::asio::ip::address loopback()
{
    return boost::asio::ip::make_address("127.0.0.1");
}

}  // namespace

// Type: unit (create success, canonical + alias targets).
// Scenario: POST with only `url`, and POST with only `target_url`, each return
//   201 whose body serializes the created link's view with the target under the
//   `url` key.
// Boundaries: both aliases resolve to the single canonical `target_url` arg;
// the
//   body's target key stays `url` (serializer contract).
// On failure first check: did the url/target_url alias handling or the 201
//   rendering change?
BOOST_AUTO_TEST_CASE(create_url_alias_and_target_url_both_succeed)
{
    const auto by_url = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com/u","slug":"c17url"})"),
        test_config());
    BOOST_TEST(by_url.result_int() == 201);
    BOOST_TEST(by_url.body().find("\"url\":\"https://example.com/u\"")
               != std::string::npos);
    BOOST_TEST(by_url.body().find("\"slug\":\"c17url\"") != std::string::npos);

    const auto by_target = dispatch(
        make_request(
            bhttp::verb::post,
            "/api/v1/links",
            R"({"target_url":"https://example.com/t","slug":"c17target"})"),
        test_config());
    BOOST_TEST(by_target.result_int() == 201);
    BOOST_TEST(by_target.body().find("\"url\":\"https://example.com/t\"")
               != std::string::npos);
}

// Type: unit (create alias precedence).
// Scenario: POST with both `url` and `target_url` present stores the `url`
//   value (url wins), returning 201.
// Boundaries: the alias order is url-first; the stored target is asserted at
// the
//   repository, not just in the body.
// On failure first check: did the alias precedence flip to target_url-first?
BOOST_AUTO_TEST_CASE(create_url_wins_over_target_url)
{
    const auto res =
        dispatch(make_request(bhttp::verb::post,
                              "/api/v1/links",
                              R"({"url":"https://example.com/win",)"
                              R"("target_url":"https://example.com/lose",)"
                              R"("slug":"c17both"})"),
                 test_config());
    BOOST_TEST(res.result_int() == 201);
    const auto stored = url_shortener::linkRepository().getBySlug("c17both");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST(stored->target_url == "https://example.com/win");
}

// Type: unit (create optional fields round-trip).
// Scenario: POST with tags, metadata, a six-key campaign, redirect_type,
//   expires_at and enabled:false returns 201 and the body reflects each field.
// Boundaries: every optional arg is rebuilt through args_builder and decoded by
//   the registry; a round-trip proves no field is dropped on the way.
// On failure first check: did an args_builder add_* mapping or an arg name
// drift
//   from the catalog?
BOOST_AUTO_TEST_CASE(create_all_optional_fields_round_trip)
{
    const std::string body =
        R"({"url":"https://example.com/full","slug":"c17full",)"
        R"("redirect_type":"permanent","expires_at":"2999-01-01T00:00:00Z",)"
        R"("enabled":false,"tags":["alpha","beta"],)"
        R"("metadata":{"team":"core","tier":"gold"},)"
        R"("campaign":{"name":"cn","source":"cs","medium":"cm",)"
        R"("term":"ct","content":"cc","id":"ci"}})";
    const auto res = dispatch(
        make_request(bhttp::verb::post, "/api/v1/links", body), test_config());
    BOOST_TEST(res.result_int() == 201);
    BOOST_TEST(res.body().find("\"redirect_type\":\"permanent\"")
               != std::string::npos);
    // The view projects enabled:false as the "disabled" status token.
    BOOST_TEST(res.body().find("\"status\":\"disabled\"") != std::string::npos);
    BOOST_TEST(res.body().find("\"alpha\"") != std::string::npos);
    BOOST_TEST(res.body().find("\"team\":\"core\"") != std::string::npos);
    BOOST_TEST(res.body().find("\"source\":\"cs\"") != std::string::npos);

    const auto stored = url_shortener::linkRepository().getBySlug("c17full");
    BOOST_REQUIRE(stored.has_value());
    BOOST_TEST(stored->redirect_type == url_shortener::RedirectType::permanent);
    BOOST_TEST(stored->enabled == false);
}

// Type: unit (campaign tri-state + unknown inner key).
// Scenario: campaign:null, campaign:{}, and a campaign with an unknown inner
// key
//   each return 201; the unknown inner key is ignored (never reaches the
//   registry's object validator, which would otherwise reject it).
// Boundaries: the REST layer rebuilds the campaign object from only the six
//   known members, so unknown inner keys are dropped before execute.
// On failure first check: did the campaign rebuild start forwarding unknown
//   keys, or did the null/empty tri-state change?
BOOST_AUTO_TEST_CASE(create_campaign_tristate_and_unknown_inner_key)
{
    const auto null_campaign = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com/cn","slug":"c17cnull",)"
                     R"("campaign":null})"),
        test_config());
    BOOST_TEST(null_campaign.result_int() == 201);

    const auto empty_campaign = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com/ce","slug":"c17cempty",)"
                     R"("campaign":{}})"),
        test_config());
    BOOST_TEST(empty_campaign.result_int() == 201);

    const auto unknown_inner = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com/cu","slug":"c17cunknown",)"
                     R"("campaign":{"source":"s","bogus":"x"}})"),
        test_config());
    BOOST_TEST(unknown_inner.result_int() == 201);
}

// Type: unit (unknown top-level key ignored).
// Scenario: a POST carrying an unknown top-level key still returns 201; the
//   unknown key is ignored (the registry would reject it, which is why args are
//   rebuilt from known keys only).
// Boundaries: only the known catalog keys are forwarded to the command.
// On failure first check: did the handler start forwarding the full body as
//   args instead of rebuilding from known keys?
BOOST_AUTO_TEST_CASE(create_unknown_top_level_key_is_ignored)
{
    const auto res = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com/x","slug":"c17unknown",)"
                     R"("surprise":"ignored"})"),
        test_config());
    BOOST_TEST(res.result_int() == 201);
}

// Type: unit (create validation: missing url).
// Scenario: a body with no url/target_url returns 400 invalid_url "url is
//   required".
// Boundaries: url is the first per-field check and wins over everything.
// On failure first check: did the required-url gate move or change its code?
BOOST_AUTO_TEST_CASE(create_missing_url_is_invalid_url)
{
    const auto res = dispatch(
        make_request(bhttp::verb::post, "/api/v1/links", "{}"), test_config());
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_url");
    BOOST_TEST(res.body().find("url is required") != std::string::npos);
}

// Type: unit (per-field 400 codes and their ORDER).
// Scenario: each malformed field reports its own code; a body with both a bad
//   redirect_type and bad tags reports invalid_redirect_type (redirect_type is
//   checked first); a missing url beats a bad redirect_type (url wins).
// Boundaries: the order of the per-field checks is the public contract and must
//   not change under the command migration.
// On failure first check: did the check order change, or did a field's code
//   drift?
BOOST_AUTO_TEST_CASE(create_per_field_codes_and_order)
{
    struct field_case
    {
        std::string body;
        std::string code;
    };
    const field_case cases[] = {
        {R"({"url":"https://example.com","redirect_type":"sideways"})",
         "invalid_redirect_type"},
        {R"({"url":"https://example.com","expires_at":"not-a-date"})",
         "invalid_expires_at"},
        {R"({"url":"https://example.com","enabled":"yes"})", "invalid_enabled"},
        {R"({"url":"https://example.com","tags":"not-array"})", "invalid_tags"},
        {R"({"url":"https://example.com","metadata":"not-object"})",
         "invalid_metadata"},
        {R"({"url":"https://example.com","campaign":5})", "invalid_campaign"},
    };
    for (const auto& c : cases) {
        const auto res =
            dispatch(make_request(bhttp::verb::post, "/api/v1/links", c.body),
                     test_config());
        BOOST_TEST(res.result_int() == 400, c.code);
        BOOST_TEST(extract_code(res.body()) == c.code, c.code);
    }

    // redirect_type is checked before tags: a body bad in both reports
    // redirect_type.
    const auto redirect_before_tags = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","redirect_type":"bad",)"
                     R"("tags":"also-bad"})"),
        test_config());
    BOOST_TEST(redirect_before_tags.result_int() == 400);
    BOOST_TEST(extract_code(redirect_before_tags.body())
               == "invalid_redirect_type");

    // Missing url wins over a bad redirect_type.
    const auto missing_url_wins = dispatch(
        make_request(
            bhttp::verb::post, "/api/v1/links", R"({"redirect_type":"bad"})"),
        test_config());
    BOOST_TEST(missing_url_wins.result_int() == 400);
    BOOST_TEST(extract_code(missing_url_wins.body()) == "invalid_url");
}

// Type: unit (deleted_at is server-managed).
// Scenario: a create body carrying deleted_at returns 400 invalid_request
//   "deleted_at is server-managed".
// Boundaries: deleted_at is the last per-field gate, after the campaign check.
// On failure first check: did the deleted_at gate disappear under the rewrite?
BOOST_AUTO_TEST_CASE(create_deleted_at_is_invalid_request)
{
    const auto res = dispatch(
        make_request(
            bhttp::verb::post,
            "/api/v1/links",
            R"({"url":"https://example.com","deleted_at":"2020-01-01T00:00:00Z"})"),
        test_config());
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_request");
    BOOST_TEST(res.body().find("deleted_at is server-managed")
               != std::string::npos);
}

// Type: unit (service-level validation surfaces as 400 invalid_request).
// Scenario: 21 tags pass the shape check (a string array) but violate the
//   service's constraint, returning 400 invalid_request "tags violate
//   constraints"; the handler never validated tag counts, the service does.
// Boundaries: the registry's generous arg caps do not pre-empt the service's
//   validator (contract C3).
// On failure first check: did the registry start rejecting the tags before the
//   service, or did the service message change?
BOOST_AUTO_TEST_CASE(create_too_many_tags_is_invalid_request)
{
    std::string tags;
    for (int i = 0; i < 21; ++i) {
        tags += (i == 0 ? "\"" : ",\"") + std::string("t") + std::to_string(i)
            + "\"";
    }
    const auto res = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","tags":[)" + tags + "]}"),
        test_config());
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_request");
    BOOST_TEST(res.body().find("tags violate constraints")
               != std::string::npos);
}

// Type: unit (long URL accepted).
// Scenario: a 5000-character URL is accepted (201); the generous arg caps never
//   reject oversized-but-valid input, leaving the service as the validator.
// Boundaries: 5000 bytes is well under the registry free-text cap.
// On failure first check: did a length cap tighten below 5000 on the create
//   path?
BOOST_AUTO_TEST_CASE(create_long_url_is_accepted)
{
    const std::string url = "https://example.com/" + std::string(5000, 'a');
    const auto res =
        dispatch(make_request(bhttp::verb::post,
                              "/api/v1/links",
                              R"({"url":")" + url + R"(","slug":"c17long"})"),
                 test_config());
    BOOST_TEST(res.result_int() == 201);
}

// Type: unit (service error mapping: reserved + conflicting slugs).
// Scenario: a reserved slug returns 409 reserved_slug; a duplicate slug returns
//   409 slug_conflict. Both codes come from the bridge's legacy mapping.
// Boundaries: reserved/conflict are service errors mapped to 409 by the bridge.
// On failure first check: did the bridge status/code columns drift from 409?
BOOST_AUTO_TEST_CASE(create_reserved_and_conflicting_slug)
{
    const auto reserved =
        dispatch(make_request(bhttp::verb::post,
                              "/api/v1/links",
                              R"({"url":"https://example.com","slug":"api"})"),
                 test_config());
    BOOST_TEST(reserved.result_int() == 409);
    BOOST_TEST(extract_code(reserved.body()) == "reserved_slug");

    const auto first = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17dupe"})"),
        test_config());
    BOOST_TEST(first.result_int() == 201);
    const auto dupe = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17dupe"})"),
        test_config());
    BOOST_TEST(dupe.result_int() == 409);
    BOOST_TEST(extract_code(dupe.body()) == "slug_conflict");
}

// Type: unit (SSRF guard).
// Scenario: a private target with shortener_allow_private_targets disabled
//   returns 400 invalid_url (the service rejects it indistinguishably).
// Boundaries: the no-process-runtime fallback service uses allow_private=false
//   (the default), matching the request config here.
// On failure first check: did the SSRF rejection stop surfacing as 400
//   invalid_url?
BOOST_AUTO_TEST_CASE(create_private_target_rejected_when_disabled)
{
    ServerConfig config;
    config.shortener_allow_private_targets = false;
    config.shortener_generated_slug_length = 8;
    const auto res = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"http://127.0.0.1/internal","slug":"c17priv"})"),
        config);
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_url");
}

// Type: unit (compat route code/slug handling).
// Scenario: on the compat POST /api/v1/short-urls route `code` is accepted as
//   the short code, `slug` wins over `code` when both are present, and the
//   canonical POST /api/v1/links route ignores `code`.
// Boundaries: CreateSlugFieldMode::slug_or_code only applies to the compat
//   route.
// On failure first check: did the slug_or_code mode leak onto the canonical
//   route, or did slug precedence flip?
BOOST_AUTO_TEST_CASE(compat_code_accepted_and_slug_precedence)
{
    const auto code_only = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/short-urls",
                     R"({"url":"https://example.com","code":"c17code"})"),
        test_config());
    BOOST_TEST(code_only.result_int() == 201);
    BOOST_TEST(code_only.body().find("\"slug\":\"c17code\"")
               != std::string::npos);

    const auto slug_wins = dispatch(
        make_request(
            bhttp::verb::post,
            "/api/v1/short-urls",
            R"({"url":"https://example.com","slug":"c17s","code":"c17c"})"),
        test_config());
    BOOST_TEST(slug_wins.result_int() == 201);
    BOOST_TEST(slug_wins.body().find("\"slug\":\"c17s\"") != std::string::npos);

    // Canonical route ignores `code`: with no slug it generates one rather than
    // using "c17ignored".
    const auto canonical = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","code":"c17ignored"})"),
        test_config());
    BOOST_TEST(canonical.result_int() == 201);
    BOOST_TEST(!url_shortener::linkRepository().getBySlug("c17ignored"));
}

// Type: unit (injection safety / hostile body).
// Scenario: a url value crafted to break out of the args JSON and inject a
//   `slug` member is escaped by args_builder; no injected link is created and
//   the response is a well-formed JSON error (the service rejects the hostile
//   URL).
// Boundaries: args_builder escapes every value; untrusted input is never
//   concatenated.
// On failure first check: did a value reach the args object unescaped (an
//   injected slug would then exist)?
BOOST_AUTO_TEST_CASE(create_hostile_body_is_escaped_without_injection)
{
    const auto res = dispatch(
        make_request(
            bhttp::verb::post,
            "/api/v1/links",
            "{\"url\":\"https://example.com/a\\\",\\\"slug\\\":\\\"pwned\"}"),
        test_config());
    // The hostile value never becomes a real slug (no JSON injection).
    BOOST_TEST(!url_shortener::linkRepository().getBySlug("pwned"));
    // The response is a well-formed JSON envelope regardless of outcome.
    BOOST_TEST(res.body().front() == '{');
    BOOST_TEST(res.body().back() == '}');
}

// Type: unit (request id propagation).
// Scenario: the X-Request-Id response header is present on both a success (201)
//   and an error (400) create.
// Boundaries: the header is set by the response helpers for every outcome.
// On failure first check: did the response helpers stop echoing the id on one
//   path?
BOOST_AUTO_TEST_CASE(create_request_id_present_on_success_and_error)
{
    const auto ok = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17rid"})"),
        test_config());
    BOOST_TEST(ok.result_int() == 201);
    BOOST_TEST(!ok.base()["X-Request-Id"].empty());

    const auto err = dispatch(
        make_request(bhttp::verb::post, "/api/v1/links", "{}"), test_config());
    BOOST_TEST(err.result_int() == 400);
    BOOST_TEST(!err.base()["X-Request-Id"].empty());
}

// Type: unit (create throttle, over limit -> 429).
// Scenario: with the create limiter wired and link_create_rate_limit = 2 from
//   one peer, the first two creates succeed (201) and the third returns 429
//   rate_limited (fixed body); the store is left with only the two created
//   links.
// Boundaries: the throttle runs after per-field validation and before execute;
//   the (N+1)th request never reaches the service.
// On failure first check: did the throttle move before validation, or did the
//   over-limit decision stop mapping to 429?
BOOST_AUTO_TEST_CASE(throttle_over_limit_returns_429)
{
    ManualClock clock {std::chrono::system_clock::time_point {}};
    auto limiter = std::make_shared<InMemoryRateLimiter>(clock);
    ServerConfig config = test_config();
    config.link_create_rate_limit = 2;
    config.link_create_rate_window_seconds = 60;
    ScopedCreateRuntime guard {config, limiter};

    for (int i = 0; i < 2; ++i) {
        const auto res = dispatch(
            make_request(bhttp::verb::post,
                         "/api/v1/links",
                         R"({"url":"https://example.com","slug":"c17th)"
                             + std::to_string(i) + "\"}"),
            guard.config,
            loopback());
        BOOST_TEST(res.result_int() == 201, "create " + std::to_string(i));
    }

    const auto blocked = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17th2"})"),
        guard.config,
        loopback());
    BOOST_TEST(blocked.result_int() == 429);
    BOOST_TEST(extract_code(blocked.body()) == "rate_limited");
    // The blocked create never reached the store.
    BOOST_TEST(!url_shortener::linkRepository().getBySlug("c17th2"));
}

// Type: unit (create throttle, key is the peer not a header, scoped prefix).
// Scenario: the limiter is called with the "link_create:"-scoped peer address
//   string even when an X-Forwarded-For header points at a different value; the
//   bucket is keyed on the peer only.
// Boundaries: X-Forwarded-For/Forwarded are deliberately not read (spoofable);
//   the "link_create:" prefix namespaces the key so it cannot collide with
//   other IRateLimiter users sharing a Redis rate_limit backend.
// On failure first check: did the throttle key start reading a header, or did
//   the "link_create:" scope prefix disappear?
BOOST_AUTO_TEST_CASE(throttle_key_is_peer_not_forwarded_header)
{
    auto limiter = std::make_shared<CapturingRateLimiter>();
    ServerConfig config = test_config();
    config.link_create_rate_limit = 5;
    ScopedCreateRuntime guard {config, limiter};

    auto req =
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17peer"})");
    req.set("X-Forwarded-For", "203.0.113.9");
    const auto res =
        dispatch(req, guard.config, boost::asio::ip::make_address("10.0.0.5"));
    BOOST_TEST(res.result_int() == 201);
    BOOST_TEST(limiter->calls == 1);
    BOOST_TEST(limiter->last_key == "link_create:10.0.0.5");
}

// Type: unit (create throttle, limiter failure is fail-open).
// Scenario: a limiter whose Allow reports a backend failure
//   (RateLimitError::unavailable) lets the create proceed (201).
// Boundaries: a deny with an error set is fail-open (Redis-down convention),
//   distinct from an over-limit deny.
// On failure first check: did the handler start treating an error-set deny as a
//   429?
BOOST_AUTO_TEST_CASE(throttle_limiter_failure_is_fail_open)
{
    auto limiter = std::make_shared<FailingRateLimiter>();
    ServerConfig config = test_config();
    config.link_create_rate_limit = 1;
    ScopedCreateRuntime guard {config, limiter};

    const auto res = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17open"})"),
        guard.config,
        loopback());
    BOOST_TEST(res.result_int() == 201);
}

// Type: unit (create throttle, disable switch).
// Scenario: link_create_rate_limit == 0 disables the throttle entirely; the
//   limiter is never called and every create succeeds even though the limiter
//   would otherwise deny.
// Boundaries: the throttle block is skipped when the limit is 0.
// On failure first check: did the 0-disables guard disappear (the deny limiter
//   would then be called)?
BOOST_AUTO_TEST_CASE(throttle_limit_zero_disables)
{
    auto limiter = std::make_shared<AlwaysDenyRateLimiter>();
    ServerConfig config = test_config();
    config.link_create_rate_limit = 0;
    ScopedCreateRuntime guard {config, limiter};

    for (int i = 0; i < 3; ++i) {
        const auto res = dispatch(
            make_request(bhttp::verb::post,
                         "/api/v1/links",
                         R"({"url":"https://example.com","slug":"c17zero)"
                             + std::to_string(i) + "\"}"),
            guard.config,
            loopback());
        BOOST_TEST(res.result_int() == 201, "create " + std::to_string(i));
    }
    BOOST_TEST(limiter->calls == 0);
}

// Type: unit (create throttle, absent peer shares a fallback bucket).
// Scenario: a request with no peer (dispatch without a peer argument) is still
//   throttled - it shares one fixed fallback bucket
//   ("link_create:peer:unknown") rather than bypassing the limiter. The
//   capturing limiter records the exact fallback key.
// Boundaries: SPEC DEVIATION - the spec said a peerless request is not
//   throttled; the security review fail-closes peerless requests to a shared
//   bucket so the throttle cannot be escaped by suppressing the peer.
// On failure first check: did the handler revert to skipping peerless requests,
//   or did the fallback key change?
BOOST_AUTO_TEST_CASE(throttle_absent_peer_shares_fallback_bucket)
{
    auto limiter = std::make_shared<CapturingRateLimiter>();
    ServerConfig config = test_config();
    config.link_create_rate_limit = 5;
    ScopedCreateRuntime guard {config, limiter};

    const auto res = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17nopeer"})"),
        guard.config);  // no peer
    BOOST_TEST(res.result_int() == 201);
    BOOST_TEST(limiter->calls == 1);
    BOOST_TEST(limiter->last_key == "link_create:peer:unknown");
}

// Type: unit (create throttle, peerless requests share one bucket -> 429).
// Scenario: with link_create_rate_limit = 1 and a real fixed-window limiter,
//   two peerless creates share the "peer:unknown" bucket, so the second is
//   throttled (429). Proves peerless requests are no longer unbounded.
// Boundaries: the shared fallback bucket is the fail-closed behavior adopted by
//   the security review (spec deviation).
// On failure first check: did peerless requests stop sharing a bucket?
BOOST_AUTO_TEST_CASE(throttle_absent_peer_shares_one_bucket_over_limit)
{
    ManualClock clock {std::chrono::system_clock::time_point {}};
    auto limiter = std::make_shared<InMemoryRateLimiter>(clock);
    ServerConfig config = test_config();
    config.link_create_rate_limit = 1;
    config.link_create_rate_window_seconds = 60;
    ScopedCreateRuntime guard {config, limiter};

    const auto first = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17np1"})"),
        guard.config);  // no peer
    BOOST_TEST(first.result_int() == 201);

    const auto second = dispatch(
        make_request(bhttp::verb::post,
                     "/api/v1/links",
                     R"({"url":"https://example.com","slug":"c17np2"})"),
        guard.config);  // no peer, same fallback bucket
    BOOST_TEST(second.result_int() == 429);
    BOOST_TEST(extract_code(second.body()) == "rate_limited");
}

// Type: unit (create limiter selection: Redis vs in-memory).
// Scenario: select_create_rate_limiter returns the configured (Redis) limiter
//   only when rate_limit.enabled && backend == "redis"; disabled or inmemory
//   backends keep the dedicated in-memory default.
// Boundaries: the selection is a pure function over the StorageConfig; stubs
//   stand in for the real limiters.
// On failure first check: did the backend-name/enabled predicate change?
BOOST_AUTO_TEST_CASE(select_create_rate_limiter_prefers_redis_when_enabled)
{
    auto configured = std::make_shared<CapturingRateLimiter>();
    auto in_memory = std::make_shared<CapturingRateLimiter>();

    StorageConfig redis;
    redis.rate_limit.enabled = true;
    redis.rate_limit.backend = "redis";
    BOOST_TEST((url_shortener::app::detail::select_create_rate_limiter(
                    redis, configured, in_memory)
                == std::static_pointer_cast<::IRateLimiter>(configured)));

    StorageConfig in_mem;
    in_mem.rate_limit.enabled = true;
    in_mem.rate_limit.backend = "inmemory";
    BOOST_TEST((url_shortener::app::detail::select_create_rate_limiter(
                    in_mem, configured, in_memory)
                == std::static_pointer_cast<::IRateLimiter>(in_memory)));

    StorageConfig disabled;  // rate_limit.enabled defaults to false
    BOOST_TEST((url_shortener::app::detail::select_create_rate_limiter(
                    disabled, configured, in_memory)
                == std::static_pointer_cast<::IRateLimiter>(in_memory)));
}

// Type: unit (peer plumbing seam, plain + TLS behavior-neutral default).
// Scenario: Router::dispatch copies the passed peer onto RouteContext::peer,
// and
//   defaults it to std::nullopt when no peer is supplied. This is the unit seam
//   under the plain/TLS session reads (the session-level remote_endpoint() read
//   is exercised end-to-end, not here).
// Boundaries: the defaulted std::nullopt keeps every existing call site
//   behavior-neutral; the redirect handlers ignore the field (C10).
// On failure first check: did dispatch stop threading peer into the context, or
//   change the defaulted value?
BOOST_AUTO_TEST_CASE(dispatch_threads_peer_into_route_context)
{
    Router router;
    std::optional<boost::asio::ip::address> seen;
    bool invoked = false;
    router.add(bhttp::verb::get,
               "/seam",
               "seam",
               [&](const BeastRequest&,
                   const ::ServerConfig&,
                   bool,
                   const RouteContext& ctx)
               {
                   invoked = true;
                   seen = ctx.peer;
                   return BeastResponse {bhttp::status::ok, 11};
               });

    const ServerConfig config;
    BeastRequest req {bhttp::verb::get, "/seam", 11};

    router.dispatch(req, config, false, loopback());
    BOOST_TEST(invoked);
    BOOST_REQUIRE(seen.has_value());
    BOOST_TEST(seen->to_string() == "127.0.0.1");

    seen.reset();
    invoked = false;
    router.dispatch(req, config, false);  // defaulted peer
    BOOST_TEST(invoked);
    BOOST_TEST(!seen.has_value());
}
