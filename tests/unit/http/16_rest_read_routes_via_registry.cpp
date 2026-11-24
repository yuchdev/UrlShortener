/** @file 16_rest_read_routes_via_registry.cpp
 *  @brief Characterizes the four REST read routes after they are re-pointed at
 *  the command registry (milestone 0006, Task 04.0 subtask 03), and pins the
 *  T-76 stats bucket-count cap enforced in the shared stats reader.
 *
 *  Scope: GET /api/v1/links/{slug}, GET /api/v1/links/id/{id},
 *  GET /api/v1/links/{slug}/preview and GET /api/v1/links/{slug}/stats. Every
 *  assertion here is a contract-C3 guard: status codes, bodies and error codes
 *  must stay byte-for-byte what they are today, the only deliberate exception
 *  being the T-76 cap, which turns a previously-accepted over-cap stats span
 *  into a 400 range_too_large (2026-11-16 user decision). The redirect fast
 * path is untouched (contract C10); this file only exercises the management
 * plane.
 */
#define BOOST_TEST_MODULE RestReadRoutesViaRegistry
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <boost/test/unit_test.hpp>
#include <url_shortener/analytics/click_event.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/link_commands.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/storage/link_repository.hpp>
#include <url_shortener/storage/memory/in_memory_click_event_repository.hpp>

namespace bhttp = boost::beast::http;
namespace commands = url_shortener::commands;
namespace composition = url_shortener::composition;
using url_shortener::Link;
using url_shortener::RedirectType;
using url_shortener::app::BuildLegacyLinkCommandService;
using url_shortener::app::GetLinkBy;
using url_shortener::app::GetLinkStatsQuery;
using url_shortener::app::LinkCommandServiceBundle;
using url_shortener::commands::CallerContext;
using url_shortener::commands::CommandErrorCode;
using url_shortener::commands::CommandRegistry;
using url_shortener::commands::CommandRequest;
using url_shortener::commands::CommandResponse;
using url_shortener::commands::register_link_commands;
using url_shortener::http::BeastRequest;
using url_shortener::http::RouterBuilder;

// The process-global analytics click store the shared stats reader aggregates
// over; declared exactly as link_handlers.cpp / the parity test do so this test
// seeds the same instance the REST route and the registry service both read.
namespace url_shortener
{
namespace storage::memory
{
class InMemoryClickEventRepository;
}
storage::memory::InMemoryClickEventRepository& analyticsClickRepository();
}  // namespace url_shortener

namespace
{

using BeastResponse = bhttp::response<bhttp::string_body>;

/**
 * @brief Shared harness: one registry + command service bundle over the same
 * global store/config the REST router reads, so a REST body and the equivalent
 * command output can be compared directly.
 *
 * With no process runtime installed the REST router falls back to the default
 * runtime over the legacy process-wide store (global `linkRepository()`); the
 * bundle here binds to the same store via `app::LinkBackend::store()`, and both
 * use a config with the default `shortener_base_domain` so `short_url` matches.
 */
struct Fixture
{
    ServerConfig config = make_config();
    LinkCommandServiceBundle bundle = BuildLegacyLinkCommandService(config);
    CommandRegistry registry;

    Fixture() { register_link_commands(registry, *bundle.service); }

    /// Dispatches a REST request through the real application router with the
    /// fixture config (the same seam the production server uses).
    BeastResponse dispatch(const BeastRequest& req) const
    {
        const auto router = RouterBuilder::buildApplicationRouter();
        return router.dispatch(req, config, false);
    }

    /// Dispatches a REST request with an explicit config override.
    BeastResponse dispatch_with(const BeastRequest& req,
                                const ServerConfig& cfg) const
    {
        const auto router = RouterBuilder::buildApplicationRouter();
        return router.dispatch(req, cfg, false);
    }

    /// Runs a command through the fixture registry as the trusted Admin caller
    /// (the role the REST resource adapter executes as, threat model T-60).
    CommandResponse run_cmd(const std::string& command,
                            const std::string& args_json) const
    {
        CommandRequest request;
        request.id = "read16";
        request.command = command;
        request.args_json = args_json;
        CallerContext caller;
        caller.principal = "read16";
        caller.transport = "test";
        caller.control_set = ControlSet::Admin;
        return registry.execute(caller, request);
    }

    static ServerConfig make_config()
    {
        ServerConfig config;
        config.shortener_allow_private_targets = true;
        config.shortener_generated_slug_length = 8;
        return config;
    }
};

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

/// Builds an active link with a fixed id derived from the slug so the by-id
/// route can address it.
Link make_seed(const std::string& slug)
{
    Link link;
    link.id = "id-" + slug;
    link.slug = slug;
    link.target_url = "https://example.com/" + slug;
    link.created_at = "2020-01-01T00:00:00Z";
    link.updated_at = "2020-01-01T00:00:00Z";
    link.enabled = true;
    link.redirect_type = RedirectType::temporary;
    return link;
}

/// Seeds a link into the shared global store and drops any stale cache entry.
void seed(const Link& link)
{
    BOOST_REQUIRE(url_shortener::linkRepository().create(link));
    url_shortener::linkCache().erase(link.slug);
}

/// Appends one click event for @p slug into the shared analytics store.
void seed_click(const std::string& slug, const std::int64_t epoch_seconds)
{
    url_shortener::analytics::ClickEvent event;
    event.event_id = "evt16-" + slug + "-" + std::to_string(epoch_seconds);
    event.occurred_at = url_shortener::analytics::Timestamp(
        std::chrono::seconds(epoch_seconds));
    event.slug = slug;
    event.domain = "example.com";
    event.status_code = 302;
    url_shortener::analyticsClickRepository().InsertBatch({event}, nullptr);
}

/// Extracts the `"code":"..."` value from a REST error body, or "" when absent.
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

}  // namespace

// Type: unit (read by slug/id, success body).
// Scenario: GET /{slug} and GET /id/{id} return 200 whose body is exactly
//   serializeLinkViewJson of the stored link's view, via link.get.
// Boundaries: both resolve through the same store the fixture bundle reads, so
//   the REST body must equal the bundle's serialized view byte-for-byte.
// On failure first check: did the handler stop routing through link.get, or did
//   serializeLinkViewJson change on one side?
BOOST_AUTO_TEST_CASE(get_by_slug_and_by_id_return_view_body)
{
    Fixture f;
    seed(make_seed("r16get"));

    const auto view = f.bundle.service->GetLink({GetLinkBy::slug, "r16get"});
    BOOST_REQUIRE(view.ok());
    const std::string expected =
        url_shortener::app::serializeLinkViewJson(*view.value);

    const auto by_slug =
        f.dispatch(make_request(bhttp::verb::get, "/api/v1/links/r16get"));
    BOOST_TEST(by_slug.result_int() == 200);
    BOOST_TEST(by_slug.body() == expected);
    BOOST_TEST(std::string(by_slug[bhttp::field::content_type])
               == "application/json");
    BOOST_TEST(!by_slug.base()["X-Request-Id"].empty());

    const auto by_id = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/id/id-r16get"));
    BOOST_TEST(by_id.result_int() == 200);
    BOOST_TEST(by_id.body() == expected);
}

// Type: unit (read, unknown target).
// Scenario: GET on an unknown slug and an unknown id both return 404 with the
//   legacy not_found body (message "Link not found").
// Boundaries: not_found originates in the service's lookup for both selectors.
// On failure first check: did the service not_found detail change, or did the
//   legacy status/code columns drift?
BOOST_AUTO_TEST_CASE(get_unknown_slug_and_id_are_not_found)
{
    Fixture f;
    for (const std::string& target :
         {"/api/v1/links/r16ghost", "/api/v1/links/id/id-r16ghost"})
    {
        const auto res = f.dispatch(make_request(bhttp::verb::get, target));
        BOOST_TEST(res.result_int() == 404);
        BOOST_TEST(extract_code(res.body()) == "not_found");
        BOOST_TEST(res.body().find("\"message\":\"Link not found\"")
                   != std::string::npos);
        BOOST_TEST(!res.base()["X-Request-Id"].empty());
    }
}

// Type: unit (preview across statuses, success body).
// Scenario: GET /preview for an active, disabled, expired and soft-deleted link
//   returns 200 with the reduced preview body and the right status token.
// Boundaries: the REST body must equal the bundle's serializeLinkPreviewJson.
// On failure first check: did PreviewLink or the preview serializer diverge, or
//   did status derivation change for one variant?
BOOST_AUTO_TEST_CASE(preview_returns_reduced_body_with_status)
{
    Fixture f;

    struct variant
    {
        std::string suffix;
        std::string status;
        std::function<void(Link&)> mutate;
    };
    const std::vector<variant> variants = {
        {"active", "active", [](Link&) {}},
        {"disabled", "disabled", [](Link& l) { l.enabled = false; }},
        {"expired",
         "expired",
         [](Link& l) { l.expires_at = "2000-01-01T00:00:00Z"; }},
        {"deleted",
         "deleted",
         [](Link& l) { l.deleted_at = "2020-06-01T00:00:00Z"; }},
    };

    for (const variant& v : variants) {
        const std::string slug = "r16prev-" + v.suffix;
        Link link = make_seed(slug);
        v.mutate(link);
        seed(link);

        const auto view =
            f.bundle.service->PreviewLink({GetLinkBy::slug, slug});
        BOOST_REQUIRE_MESSAGE(view.ok(), v.suffix);
        const std::string expected =
            url_shortener::app::serializeLinkPreviewJson(*view.value);

        const auto res = f.dispatch(make_request(
            bhttp::verb::get, "/api/v1/links/" + slug + "/preview"));
        BOOST_TEST(res.result_int() == 200, v.suffix);
        BOOST_TEST(res.body() == expected, v.suffix);
        BOOST_TEST(res.body().find("\"status\":\"" + v.status + "\"")
                       != std::string::npos,
                   v.suffix);
    }
}

// Type: unit (preview, unknown).
// Scenario: GET /preview on an unknown slug returns 404 not_found.
// Boundaries: not_found originates in PreviewLink's lookup.
// On failure first check: did the preview handler stop surfacing not_found?
BOOST_AUTO_TEST_CASE(preview_unknown_is_not_found)
{
    Fixture f;
    const auto res = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/r16prevghost/preview"));
    BOOST_TEST(res.result_int() == 404);
    BOOST_TEST(extract_code(res.body()) == "not_found");
}

// Type: unit (stats range mode, success body).
// Scenario: GET /stats?from&to&bucket over seeded click events returns 200 with
//   exactly serializeLinkStatsJson for the same query.
// Boundaries: a bounded (under-cap) window; both sides read the shared
// analytics
//   store through the same reader.
// On failure first check: did GetLinkStats or serializeLinkStatsJson diverge,
// or
//   were differing events seeded?
BOOST_AUTO_TEST_CASE(stats_range_mode_returns_stats_body)
{
    Fixture f;
    seed(make_seed("r16statsr"));
    seed_click("r16statsr", 1500000000);
    seed_click("r16statsr", 1500100000);

    GetLinkStatsQuery query;
    query.slug = "r16statsr";
    query.from = "1499000000";
    query.to = "1501000000";
    query.bucket = "day";
    const auto view = f.bundle.service->GetLinkStats(query);
    BOOST_REQUIRE(view.ok());
    const std::string expected =
        url_shortener::app::serializeLinkStatsJson(*view.value);

    const auto res = f.dispatch(
        make_request(bhttp::verb::get,
                     "/api/v1/links/r16statsr/"
                     "stats?from=1499000000&to=1501000000&bucket=day"));
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == expected);
}

// Type: unit (stats range validation).
// Scenario: from after to, a non-numeric from, and an unknown bucket each
// return
//   400 with the legacy promoted codes invalid_timestamp / invalid_timestamp /
//   invalid_bucket.
// Boundaries: the reader is the sole validator; these details are promoted to
//   REST codes by the bridge (T-62), unlike range_too_large.
// On failure first check: did the reader stop emitting a detail, or did the
//   bridge's promotion set change?
BOOST_AUTO_TEST_CASE(stats_range_validation_errors_keep_promoted_codes)
{
    Fixture f;
    seed(make_seed("r16statsv"));

    const auto from_after_to = f.dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/r16statsv/stats?from=2000&to=1000&bucket=day"));
    BOOST_TEST(from_after_to.result_int() == 400);
    BOOST_TEST(extract_code(from_after_to.body()) == "invalid_timestamp");

    const auto non_numeric = f.dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/r16statsv/stats?from=abc&to=1000&bucket=day"));
    BOOST_TEST(non_numeric.result_int() == 400);
    BOOST_TEST(extract_code(non_numeric.body()) == "invalid_timestamp");

    const auto bad_bucket = f.dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/r16statsv/stats?from=1000&to=2000&bucket=year"));
    BOOST_TEST(bad_bucket.result_int() == 400);
    BOOST_TEST(extract_code(bad_bucket.body()) == "invalid_bucket");
}

// Type: unit (stats epoch range guard, T-76 overflow fix / security 04.0/03).
// Scenario: window bounds outside [1970-01-01T00:00:00Z, 9999-12-31T23:59:59Z]
//   — a negative from, a to one second past year 9999, a from beyond year 9999,
//   and the extreme pair from=-5000000000&to=5000000000&bucket=hour whose
//   native-resolution span would overflow — are each rejected as 400
//   invalid_timestamp before any time_point arithmetic, so the overflow can
//   never wrap negative and skip the bucket cap. None ever returns 200.
// Boundaries: parseEpoch validates the integer in [0, 253402300799] BEFORE
//   building a time_point; the reader emits invalid_timestamp, which the bridge
//   promotes to that REST code (T-62). This changes REST behaviour only for
//   absurd epochs previously accepted/UB (user-approved cap decision).
// On failure first check: did parseEpoch drop its range check, or did the span
//   go back to subtracting system_clock time_points?
BOOST_AUTO_TEST_CASE(stats_out_of_range_epochs_are_invalid_timestamp)
{
    Fixture f;
    seed_click("r16epoch", 1000000);  // In range; must never be aggregated.

    const std::vector<std::string> queries = {
        "from=-1&to=1000&bucket=day",  // negative from
        "from=0&to=253402300800&bucket=day",  // to one past 9999
        "from=253402300800&to=253402300900&bucket=day",  // from beyond 9999
        "from=-5000000000&to=5000000000&bucket=hour",  // extreme overflow pair
    };
    for (const std::string& q : queries) {
        const auto res = f.dispatch(make_request(
            bhttp::verb::get, "/api/v1/links/r16epoch/stats?" + q));
        BOOST_TEST(res.result_int() == 400, q);
        BOOST_TEST(extract_code(res.body()) == "invalid_timestamp", q);
        // Never aggregated: an error envelope carries no stats fields.
        BOOST_TEST(res.body().find("total_attempts") == std::string::npos, q);
    }
}

// Type: unit (stats bucket cap, T-76 over-cap rejection).
// Scenario: an over-cap span (from=0&to=9999999999&bucket=hour, > 10,000 hour
//   buckets) returns 400 invalid_request with message range_too_large, and no
//   aggregate stats body is produced even with a seeded event in range.
// Boundaries: the cap short-circuits before GetAggregateStats, so the body is
// an
//   error envelope with no total_attempts/time_buckets fields. range_too_large
//   is NOT promoted to a REST code (stays invalid_request), unlike the other
//   two stats details.
// On failure first check: did the cap move after GetAggregateStats, or did the
//   bridge start promoting range_too_large?
BOOST_AUTO_TEST_CASE(stats_over_cap_span_is_rejected_before_aggregation)
{
    Fixture f;
    seed_click("r16statscap", 1000000);  // In range; must never be aggregated.

    const auto res = f.dispatch(make_request(
        bhttp::verb::get,
        "/api/v1/links/r16statscap/stats?from=0&to=9999999999&bucket=hour"));
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(extract_code(res.body()) == "invalid_request");
    BOOST_TEST(res.body().find("range_too_large") != std::string::npos);
    // Nothing was aggregated: an error envelope carries no stats fields.
    BOOST_TEST(res.body().find("total_attempts") == std::string::npos);
    BOOST_TEST(res.body().find("time_buckets") == std::string::npos);
    // The requested range is never echoed back (fixed, value-free detail).
    BOOST_TEST(res.body().find("9999999999") == std::string::npos);
}

// Type: unit (stats bucket cap, exact boundary per bucket).
// Scenario: for hour, day and week, a span of exactly 10,000 buckets is
// accepted
//   (200) and a span of 10,001 buckets is rejected (400 range_too_large).
// Boundaries: the cap is (to-from)/width > 10,000; from=0 makes the arithmetic
//   exact. Range mode does not require the slug to exist, so an empty-aggregate
//   200 is the accepted outcome.
// On failure first check: did the width constants or the >-vs->= comparison
//   change?
BOOST_AUTO_TEST_CASE(stats_bucket_cap_boundary_is_exact_for_each_bucket)
{
    Fixture f;

    struct bucket_case
    {
        std::string bucket;
        long long width;
    };
    const std::vector<bucket_case> cases = {
        {"hour", 3600}, {"day", 86400}, {"week", 604800}};

    for (const bucket_case& c : cases) {
        const long long at_cap = c.width * 10000;
        const long long over_cap = c.width * 10001;

        const auto accepted = f.dispatch(make_request(
            bhttp::verb::get,
            "/api/v1/links/r16cap/stats?from=0&to=" + std::to_string(at_cap)
                + "&bucket=" + c.bucket));
        BOOST_TEST(accepted.result_int() == 200, c.bucket + " at-cap");

        const auto rejected = f.dispatch(make_request(
            bhttp::verb::get,
            "/api/v1/links/r16cap/stats?from=0&to=" + std::to_string(over_cap)
                + "&bucket=" + c.bucket));
        BOOST_TEST(rejected.result_int() == 400, c.bucket + " over-cap");
        BOOST_TEST(extract_code(rejected.body()) == "invalid_request",
                   c.bucket + " over-cap code");
        BOOST_TEST(rejected.body().find("range_too_large") != std::string::npos,
                   c.bucket + " over-cap message");
    }
}

// Type: unit (stats bucket cap, REST / link.stats parity).
// Scenario: the same over-cap span through the link.stats command returns an
//   invalid_field failure with message range_too_large.
// Boundaries: the cap lives in the shared reader, so REST (invalid_request) and
//   the command (invalid_field) differ only in the adapter-level code, with the
//   identical fixed detail message.
// On failure first check: did the cap stop being shared, or did the command
//   envelope stop carrying the detail message?
BOOST_AUTO_TEST_CASE(stats_over_cap_command_parity)
{
    Fixture f;
    const auto cmd = f.run_cmd(
        "link.stats",
        R"({"slug":"r16cap","from":"0","to":"9999999999","bucket":"hour"})");
    BOOST_TEST(!cmd.ok);
    BOOST_TEST((cmd.error.code == CommandErrorCode::invalid_field));
    BOOST_TEST(cmd.error.message == "range_too_large");
}

// Type: unit (stats lifetime mode, no window).
// Scenario: GET /stats with no query returns 200 with the six-key lifetime body
//   (serializeLinkLifetimeStatsJson).
// Boundaries: the no-window branch sends only slug and the command falls back
// to
//   the lifetime counters.
// On failure first check: did the lifetime serializer drift, or did the handler
//   stop detecting the no-window case?
BOOST_AUTO_TEST_CASE(stats_lifetime_no_query_returns_lifetime_body)
{
    Fixture f;
    Link link = make_seed("r16statsl");
    link.stats.total_redirects = 5;
    link.stats.redirects_24h = 2;
    link.stats.redirects_7d = 4;
    seed(link);

    const auto view = f.bundle.service->GetLink({GetLinkBy::slug, "r16statsl"});
    BOOST_REQUIRE(view.ok());
    const std::string expected =
        url_shortener::app::serializeLinkLifetimeStatsJson(*view.value);

    const auto res = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/r16statsl/stats"));
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body() == expected);
    BOOST_TEST(res.body().find("\"total_redirects\":5") != std::string::npos);
}

// Type: unit (stats lifetime mode, partial window ignored).
// Scenario: GET /stats?from=... with only one of the three range params still
//   falls back to the lifetime body (today's partial-range contract).
// Boundaries: range mode requires all three of from/to/bucket to be non-empty.
// On failure first check: did the all-three guard change to an any-of guard?
BOOST_AUTO_TEST_CASE(stats_lifetime_partial_window_falls_back)
{
    Fixture f;
    seed(make_seed("r16statsp"));

    const auto res = f.dispatch(make_request(
        bhttp::verb::get, "/api/v1/links/r16statsp/stats?from=1000000000"));
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body().find("\"total_redirects\":") != std::string::npos);
    BOOST_TEST(res.body().find("\"created_at\":") != std::string::npos);
}

// Type: unit (stats lifetime mode, unknown slug).
// Scenario: GET /stats with no window on an unknown slug returns 404 not_found.
// Boundaries: the lifetime branch resolves the link through GetLink, which
//   surfaces not_found.
// On failure first check: did the lifetime branch stop checking existence?
BOOST_AUTO_TEST_CASE(stats_lifetime_unknown_slug_is_not_found)
{
    Fixture f;
    const auto res = f.dispatch(
        make_request(bhttp::verb::get, "/api/v1/links/r16statsghost/stats"));
    BOOST_TEST(res.result_int() == 404);
    BOOST_TEST(extract_code(res.body()) == "not_found");
}

// Type: unit (method dispatch on read paths).
// Scenario: POST and HEAD on the read paths are 405 invalid_method with an
//   Allow header that lists GET (RFC 9110 15.5.6); the migrated handlers never
//   run.
// Boundaries: method checking is a router concern; every read path serves GET,
//   so the Allow header always includes GET (the {slug} path also lists PATCH
//   and DELETE). HEAD is never advertised.
// On failure first check: did a read route start accepting a non-GET method, or
//   did the Allow header stop listing GET?
BOOST_AUTO_TEST_CASE(non_get_methods_on_read_paths_are_invalid_method)
{
    Fixture f;
    seed(make_seed("r16method"));

    const std::vector<std::string> paths = {"/api/v1/links/r16method",
                                            "/api/v1/links/r16method/preview",
                                            "/api/v1/links/r16method/stats"};
    for (const std::string& path : paths) {
        for (const bhttp::verb method : {bhttp::verb::post, bhttp::verb::head})
        {
            const auto res = f.dispatch(make_request(method, path));
            BOOST_TEST(res.result_int() == 405, path);
            BOOST_TEST(extract_code(res.body()) == "invalid_method", path);
            const std::string allow =
                std::string(res.base()[bhttp::field::allow]);
            BOOST_TEST(allow.find("GET") != std::string::npos, path);
        }
    }
}

// Type: unit (configured base domain flows into short_url).
// Scenario: with a process runtime built from a config whose
//   shortener_base_domain is customized, a REST GET renders short_url with that
//   base, proving the configured base reaches the response through the
//   installed runtime (the process-wide model from 04.0/02).
// Boundaries: since 04.0/02 the runtime is process-wide and config-bound at
//   install time, not per-request; this case installs and then resets it so no
//   other case is affected.
// On failure first check: did build_command_runtime stop threading the base
//   domain into serializeLinkViewJson, or did the handler stop using the
//   installed runtime?
BOOST_AUTO_TEST_CASE(configured_base_domain_flows_into_short_url)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();

    ServerConfig configured;
    configured.shortener_allow_private_targets = true;
    configured.shortener_generated_slug_length = 8;
    configured.shortener_base_domain = "https://short.example";

    composition::install_process_command_runtime(
        std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(
                configured, url_shortener::app::LinkBackendHandle {})));

    seed(make_seed("r16base"));

    const auto router = RouterBuilder::buildApplicationRouter();
    const auto res =
        router.dispatch(make_request(bhttp::verb::get, "/api/v1/links/r16base"),
                        configured,
                        false);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(
        res.body().find("\"short_url\":\"https://short.example/r16base\"")
        != std::string::npos);

    // Teardown: restore the process-wide defaults for any following case.
    composition::reset_process_command_runtime();
    url_shortener::app::LinkBackend::reset();
}
