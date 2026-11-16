/** @file 23_rest_commands_endpoint.cpp
 *  @brief Characterizes the generic POST /api/v1/commands endpoint (milestone
 *  0006, Task 04.0 subtask 09): the config gate, the required JSON content
 *  type, the full envelope hardening limits, the gated caller model, the
 *  shared per-peer create throttle (including dry_run), and the envelope /
 *  error-status mapping.
 *
 *  Numbering note: the spec named the file 22, but 22 (list route) and 24
 *  (command_api_caller) are already taken, so this endpoint test is 23 (the
 *  next free slot in the 15-29 http band).
 *
 *  Security: gate off by default (threat model T-64), JSON content type
 *  required (T-67), full envelope limits reach the scanner (T-68), admin only
 *  for a loopback peer (T-64/T-70), manage_users refused on every path (T-61),
 *  link.create throttled per peer incl. dry_run (T-75), the body never echoed
 *  (T-69). The redirect fast path is untouched (contract C10).
 */
#define BOOST_TEST_MODULE RestCommandsEndpoint
#include <boost/test/unit_test.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/ip/address.hpp>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/clock.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/request_handlers.hpp>
#include <url_shortener/http/route_registry.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/storage/i_rate_limiter.hpp>
#include <url_shortener/storage/inmemory/in_memory_rate_limiter.hpp>
#include <url_shortener/storage/link_repository.hpp>

namespace bhttp = boost::beast::http;
namespace commands = url_shortener::commands;
namespace composition = url_shortener::composition;
using url_shortener::http::BeastRequest;
using url_shortener::http::BeastResponse;
using url_shortener::http::RouterBuilder;

namespace
{

const boost::asio::ip::address k_loopback =
    boost::asio::ip::make_address("127.0.0.1");
const boost::asio::ip::address k_remote =
    boost::asio::ip::make_address("10.0.0.5");

ServerConfig make_config(const bool enabled, const std::string& role)
{
    ServerConfig config;
    config.command_api_enabled = enabled;
    config.command_api_role = role;
    config.shortener_allow_private_targets = true;
    return config;
}

BeastResponse dispatch(const BeastRequest& req,
                       const ServerConfig& config,
                       std::optional<boost::asio::ip::address> peer =
                           std::nullopt)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, config, false, std::move(peer));
}

/// Builds a POST request to a target with a body and (optionally) a
/// Content-Type header. When @p content_type is empty the header is omitted.
BeastRequest make_request(const std::string& target,
                          const std::string& body,
                          const std::string& content_type = "application/json")
{
    BeastRequest req {bhttp::verb::post, target, 11};
    req.set(bhttp::field::host, "short.test");
    if (!content_type.empty()) {
        req.set(bhttp::field::content_type, content_type);
    }
    req.body() = body;
    req.prepare_payload();
    return req;
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

/// Extracts the value of a `"<key>":"<value>"` string field from an envelope
/// body, or "" when absent.
std::string string_field(const std::string& body, const std::string& key)
{
    const std::string needle = "\"" + key + "\":\"";
    const auto pos = body.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + needle.size();
    const auto end = body.find('"', start);
    return end == std::string::npos ? std::string {}
                                    : body.substr(start, end - start);
}

/// The envelope `error.code` wire name, or "" for a success envelope.
std::string error_code(const std::string& body)
{
    return string_field(body, "code");
}

/// Installs a process runtime (built from the fixture config) with an explicit
/// create limiter so the handler's throttle uses a limiter the test controls,
/// mirroring the pattern used by 17_rest_create_via_registry.
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

/// A limiter that records the key it saw and always permits.
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

/// A limiter that always reports a backend failure (to prove fail-open).
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

/// A limiter that always denies without error (to prove the disable switch).
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

}  // namespace

// Type: unit (gate off, byte-equal to an unmatched route, body not parsed).
// Scenario: with the default config (command_api_enabled == false), POST
//   /api/v1/commands returns exactly the unmatched-route 404 body and never
//   parses the envelope.
// Boundaries: the gate is the whole perimeter (threat model T-64); off means
//   "route does not exist", byte-identical to the router's own 404.
// On failure first check: did the gate branch change its status/message, or did
//   it start parsing/echoing the body?
BOOST_AUTO_TEST_CASE(gate_off_is_404_byte_equal_and_body_not_parsed)
{
    const ServerConfig config = make_config(false, "user");

    // A body that WOULD parse to a valid envelope if the handler read it.
    const std::string body =
        R"({"v":1,"command":"link.list","args":{},"id":"SENTINEL-ID"})";
    BeastRequest req = make_request("/api/v1/commands", body);
    req.set("X-Internal-Request-Id", "req-gate-off");
    const auto res = dispatch(req, config, k_remote);

    // The unmatched-route baseline: a path with no registered route.
    BeastRequest unmatched = make_request("/api/v1/zzz-no-route", body);
    unmatched.set("X-Internal-Request-Id", "req-gate-off");
    const auto baseline = dispatch(unmatched, config, k_remote);

    BOOST_TEST(res.result_int() == 404);
    BOOST_TEST(error_code(res.body()) == "not_found");
    BOOST_TEST(res.body() == baseline.body());
    // The body was not parsed: no envelope markers, no echoed id/command.
    BOOST_TEST(!contains(res.body(), "SENTINEL-ID"));
    BOOST_TEST(!contains(res.body(), "\"ok\":"));
    BOOST_TEST(!contains(res.body(), "link.list"));
}

// Type: unit (required JSON content type, T-67).
// Scenario: a missing Content-Type, text/plain, and
//   application/x-www-form-urlencoded all return 415; application/json,
//   application/json; charset=utf-8, and Application/JSON are accepted (not
//   415). The body is not parsed on a 415.
// Boundaries: only the media type matters (params ignored, case-insensitive);
//   415 forces a CORS preflight the server never grants.
// On failure first check: did the media-type parse start reading parameters or
//   become case-sensitive?
BOOST_AUTO_TEST_CASE(content_type_must_be_application_json)
{
    const ServerConfig config = make_config(true, "user");
    const std::string body = R"({"v":1,"command":"link.list","args":{}})";

    for (const std::string& ct :
         {std::string(""), std::string("text/plain"),
          std::string("application/x-www-form-urlencoded")}) {
        const auto res =
            dispatch(make_request("/api/v1/commands", body, ct), config,
                     k_loopback);
        BOOST_TEST(res.result_int() == 415, "ct=[" + ct + "]");
        BOOST_TEST(error_code(res.body()) == "unsupported_media_type",
                   "ct=[" + ct + "]");
        // The 415 refusal never parses or echoes the envelope.
        BOOST_TEST(!contains(res.body(), "\"ok\":"), "ct=[" + ct + "]");
    }

    for (const std::string& ct :
         {std::string("application/json"),
          std::string("application/json; charset=utf-8"),
          std::string("Application/JSON")}) {
        const auto res =
            dispatch(make_request("/api/v1/commands", body, ct), config,
                     k_loopback);
        BOOST_TEST(res.result_int() != 415, "ct=[" + ct + "]");
    }
}

// Type: unit (gate on, role user: read ok, write denied).
// Scenario: under role user, link.list succeeds (200, ok:true), while
//   link.create is denied (403 not_authorized envelope) and nothing is stored.
// Boundaries: authorization happens inside execute (C7); a User caller may read
//   but not write.
// On failure first check: did the gated caller stop defaulting to User, or did
//   link.list/link.create change permission?
BOOST_AUTO_TEST_CASE(user_role_lists_but_cannot_create)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const ServerConfig config = make_config(true, "user");

    const auto listed =
        dispatch(make_request("/api/v1/commands",
                              R"({"v":1,"command":"link.list","args":{}})"),
                 config, k_remote);
    BOOST_TEST(listed.result_int() == 200);
    BOOST_TEST(contains(listed.body(), "\"ok\":true"));

    const auto created = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.create",)"
            R"("args":{"target_url":"https://example.com/u23","slug":"c23u"}})"),
        config, k_remote);
    BOOST_TEST(created.result_int() == 403);
    BOOST_TEST(error_code(created.body()) == "not_authorized");
    BOOST_TEST(!url_shortener::linkRepository().getBySlug("c23u"));
}

// Type: unit (gate on, loopback admin: full create/get/update/disable/delete).
// Scenario: a loopback caller under role admin creates a link (200, result has
//   the slug), then reads, updates, disables and deletes it, each 200 ok:true.
// Boundaries: Admin is honoured only for a loopback peer; the generic endpoint
//   uses 200 for an ok result (not REST's 201).
// On failure first check: did the loopback+admin escalation stop granting
//   Admin, or did a lifecycle command regress?
BOOST_AUTO_TEST_CASE(loopback_admin_full_lifecycle)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const ServerConfig config = make_config(true, "admin");

    const auto created = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.create",)"
            R"("args":{"target_url":"https://example.com/l23","slug":"c23a"}})"),
        config, k_loopback);
    BOOST_TEST(created.result_int() == 200);
    BOOST_TEST(contains(created.body(), "\"ok\":true"));
    BOOST_TEST(contains(created.body(), "\"slug\":\"c23a\""));

    const auto got = dispatch(
        make_request("/api/v1/commands",
                     R"({"v":1,"command":"link.get","args":{"slug":"c23a"}})"),
        config, k_loopback);
    BOOST_TEST(got.result_int() == 200);
    BOOST_TEST(contains(got.body(), "\"slug\":\"c23a\""));

    const auto updated = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.update",)"
            R"("args":{"slug":"c23a","tags":["x"]}})"),
        config, k_loopback);
    BOOST_TEST(updated.result_int() == 200);
    BOOST_TEST(contains(updated.body(), "\"ok\":true"));

    const auto disabled = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.disable","args":{"slug":"c23a"}})"),
        config, k_loopback);
    BOOST_TEST(disabled.result_int() == 200);

    const auto deleted = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.delete","args":{"slug":"c23a"}})"),
        config, k_loopback);
    BOOST_TEST(deleted.result_int() == 200);
    BOOST_TEST(contains(deleted.body(), "\"ok\":true"));
}

// Type: unit (role admin honoured only for a loopback peer, T-64).
// Scenario: role admin but a non-loopback peer, and role admin with no peer,
//   both run as User, so link.create is 403 not_authorized.
// Boundaries: the loopback peer check is the only locality guard; an
//   absent/remote peer never escalates (fail-closed).
// On failure first check: did the peer check become an OR, or did an
//   absent/remote peer get Admin?
BOOST_AUTO_TEST_CASE(admin_role_requires_loopback_peer)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const ServerConfig config = make_config(true, "admin");
    const std::string create =
        R"({"v":1,"command":"link.create",)"
        R"("args":{"target_url":"https://example.com/x23","slug":"c23r"}})";

    const auto remote =
        dispatch(make_request("/api/v1/commands", create), config, k_remote);
    BOOST_TEST(remote.result_int() == 403);
    BOOST_TEST(error_code(remote.body()) == "not_authorized");

    // No peer argument at all (absent == non-loopback == User).
    const auto no_peer = dispatch(make_request("/api/v1/commands", create),
                                  config);
    BOOST_TEST(no_peer.result_int() == 403);
    BOOST_TEST(error_code(no_peer.body()) == "not_authorized");
    BOOST_TEST(!url_shortener::linkRepository().getBySlug("c23r"));
}

// Type: unit (manage_users refused on every role/peer, T-61).
// Scenario: with a stub manage_users descriptor installed as the process
//   runtime, POST /api/v1/commands for it is 403 not_authorized for role user
//   and role admin, from a loopback and a remote peer, and the handler never
//   runs.
// Boundaries: rest_policy_refusal is a transport rule applied before execute
//   (D4), independent of the caller's control_set.
// On failure first check: did the refusal stop keying on manage_users, or did
//   the handler get entered?
BOOST_AUTO_TEST_CASE(manage_users_is_refused_on_every_path)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();

    bool handler_called = false;
    auto registry = std::make_shared<commands::CommandRegistry>();
    {
        commands::CommandDescriptor d;
        d.name = "user.create";
        d.summary = "stub user.create";
        d.permission = commands::Permission::manage_users;
        d.kind = commands::ExecutionKind::sync;
        d.handler = [&handler_called](commands::CommandContext&,
                                      const commands::ArgsView&) {
            handler_called = true;
            commands::CommandResult r;
            r.result_json = R"({"stub":true})";
            return r;
        };
        registry->add(std::move(d));
    }
    auto runtime = std::make_shared<composition::CommandRuntime>();
    runtime->registry = registry;
    composition::install_process_command_runtime(std::move(runtime));

    const std::string body = R"({"v":1,"command":"user.create","args":{}})";
    for (const std::string& role : {std::string("user"), std::string("admin")}) {
        const ServerConfig config = make_config(true, role);
        for (const auto& peer :
             {std::optional<boost::asio::ip::address>(k_loopback),
              std::optional<boost::asio::ip::address>(k_remote)}) {
            const auto res =
                dispatch(make_request("/api/v1/commands", body), config, peer);
            BOOST_TEST(res.result_int() == 403, role);
            BOOST_TEST(error_code(res.body()) == "not_authorized", role);
        }
    }
    BOOST_TEST(!handler_called);

    composition::reset_process_command_runtime();
}

// Type: unit (envelope parse errors -> invalid_envelope / unknown_command, T-68).
// Scenario: not-JSON, missing command, v:2, an unknown top-level key, an
//   over-long id, a depth-9 body, and a 257-member object all return 400
//   invalid_envelope; an unknown command returns 404 unknown_command; bad args
//   return 400 invalid_field. No response echoes the raw body.
// Boundaries: the body is parsed only by parse_request under the full hardening
//   limits; the member (256) and depth (8) caps must survive the partial
//   envelope_limits override (T-68).
// On failure first check: did a cap get disabled, or did a code/status drift?
BOOST_AUTO_TEST_CASE(envelope_errors_map_to_table_status)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    // admin+loopback so link.create reaches arg validation (not a 403).
    const ServerConfig config = make_config(true, "admin");

    const std::string long_id(100, 'z');
    std::string deep = R"({"v":1,"command":"a.b","args":)";
    deep += std::string(12, '[') + std::string(12, ']') + "}";
    // 257 TOP-LEVEL members: the scanner's member cap (256) is enforced only on
    // the object it scans (the envelope), not on a nested args value, so the
    // over-member object must be the envelope itself to exercise the cap (T-68).
    std::string wide = "{";
    for (int i = 0; i < 257; ++i) {
        wide += (i == 0 ? "" : ",");
        wide += "\"k" + std::to_string(i) + "\":1";
    }
    wide += "}";

    struct envelope_case
    {
        std::string body;
        int status;
        std::string code;
    };
    const envelope_case cases[] = {
        {"not json at all", 400, "invalid_envelope"},
        {R"({"v":1})", 400, "invalid_envelope"},
        {R"({"v":2,"command":"link.list"})", 400, "invalid_envelope"},
        {R"({"v":1,"command":"link.list","surprise":1})", 400,
         "invalid_envelope"},
        {R"({"v":1,"command":"link.list","id":")" + long_id + "\"}", 400,
         "invalid_envelope"},
        {deep, 400, "invalid_envelope"},
        {wide, 400, "invalid_envelope"},
        {R"({"v":1,"command":"no.such.command","args":{}})", 404,
         "unknown_command"},
        {R"({"v":1,"command":"link.create","args":{}})", 400, "invalid_field"},
    };
    for (const auto& c : cases) {
        const auto res =
            dispatch(make_request("/api/v1/commands", c.body), config,
                     k_loopback);
        BOOST_TEST(res.result_int() == c.status, c.body.substr(0, 40));
        BOOST_TEST(error_code(res.body()) == c.code, c.body.substr(0, 40));
    }

    // The over-long id is never echoed back (threat model T-05/T-69).
    const auto id_res = dispatch(
        make_request("/api/v1/commands",
                     R"({"v":1,"command":"link.list","id":")" + long_id + "\"}"),
        config, k_loopback);
    BOOST_TEST(!contains(id_res.body(), long_id));
}

// Type: unit (service error codes -> table status).
// Scenario: a reserved slug -> 409 reserved_slug; a conflicting slug -> 409
//   slug_conflict; a missing link.get -> 404 not_found; db.rows for an
//   unknown table -> 404 not_found.
// Boundaries: every code maps through the one shared error table
//   (http_status_for), reachable without jobs.
// On failure first check: did a status column drift, or did db.rows echo the
//   table name?
BOOST_AUTO_TEST_CASE(service_errors_map_to_table_status)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const ServerConfig config = make_config(true, "admin");

    const auto reserved = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.create",)"
            R"("args":{"target_url":"https://example.com/a","slug":"api"}})"),
        config, k_loopback);
    BOOST_TEST(reserved.result_int() == 409);
    BOOST_TEST(error_code(reserved.body()) == "reserved_slug");

    const std::string create_dupe =
        R"({"v":1,"command":"link.create",)"
        R"("args":{"target_url":"https://example.com/d","slug":"c23dupe"}})";
    BOOST_TEST(dispatch(make_request("/api/v1/commands", create_dupe), config,
                        k_loopback)
                   .result_int()
               == 200);
    const auto dupe = dispatch(make_request("/api/v1/commands", create_dupe),
                               config, k_loopback);
    BOOST_TEST(dupe.result_int() == 409);
    BOOST_TEST(error_code(dupe.body()) == "slug_conflict");

    const auto missing = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.get","args":{"slug":"c23zznope"}})"),
        config, k_loopback);
    BOOST_TEST(missing.result_int() == 404);
    BOOST_TEST(error_code(missing.body()) == "not_found");

    const auto db_users = dispatch(
        make_request("/api/v1/commands",
                     R"({"v":1,"command":"db.rows","args":{"table":"users"}})"),
        config, k_loopback);
    BOOST_TEST(db_users.result_int() == 404);
    BOOST_TEST(error_code(db_users.body()) == "not_found");
    BOOST_TEST(!contains(db_users.body(), "users"));
}

// Type: unit (dry_run on a non-dry-run command -> 501 unsupported).
// Scenario: link.get does not support dry_run, so a request carrying
//   options.dry_run returns 501 unsupported; this proves request.options reach
//   execute.
// Boundaries: the generic endpoint preserves options (unlike the resource
//   overload); the registry maps an unsupported mode to 501.
// On failure first check: did the handler drop options, or did unsupported stop
//   mapping to 501?
BOOST_AUTO_TEST_CASE(dry_run_on_non_dry_run_command_is_unsupported)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const ServerConfig config = make_config(true, "user");

    const auto res = dispatch(
        make_request("/api/v1/commands",
                     R"({"v":1,"command":"link.get",)"
                     R"("args":{"slug":"c23anything"},"options":{"dry_run":true}})"),
        config, k_remote);
    BOOST_TEST(res.result_int() == 501);
    BOOST_TEST(error_code(res.body()) == "unsupported");
}

// Type: unit (id echo and server-generated fallback).
// Scenario: a provided valid id is echoed in the response envelope; an absent
//   id yields a server-generated "srv-<n>" id.
// Boundaries: id handling is the parser's (threat model T-05); the response
//   always carries an id to echo.
// On failure first check: did the id stop round-tripping, or did the srv-
//   fallback change shape?
BOOST_AUTO_TEST_CASE(id_is_echoed_or_server_generated)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    const ServerConfig config = make_config(true, "user");

    const auto echoed = dispatch(
        make_request(
            "/api/v1/commands",
            R"({"v":1,"command":"link.list","args":{},"id":"my-id-123"})"),
        config, k_remote);
    BOOST_TEST(string_field(echoed.body(), "id") == "my-id-123");

    const auto generated =
        dispatch(make_request("/api/v1/commands",
                              R"({"v":1,"command":"link.list","args":{}})"),
                 config, k_remote);
    BOOST_TEST(contains(generated.body(), "\"id\":\"srv-"));
}

// Type: unit (body larger than max_request_body_bytes -> 400 invalid_envelope).
// Scenario: with a tiny max_request_body_bytes, an over-size envelope is
//   rejected as invalid_envelope by the parser's byte cap (the session-layer
//   413 happens earlier in production and is covered by session tests).
// Boundaries: max_bytes is the one envelope_limits field this handler sets from
//   config; the parser re-checks it.
// On failure first check: did the handler stop forwarding max_bytes, or did the
//   parser stop enforcing it?
BOOST_AUTO_TEST_CASE(oversize_body_is_invalid_envelope)
{
    url_shortener::app::LinkBackend::reset();
    composition::reset_process_command_runtime();
    ServerConfig config = make_config(true, "user");
    config.max_request_body_bytes = 40;

    const std::string body =
        R"({"v":1,"command":"link.list","args":{"q":")"
        + std::string(200, 'a') + "\"}}";
    const auto res =
        dispatch(make_request("/api/v1/commands", body), config, k_remote);
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(error_code(res.body()) == "invalid_envelope");
}

// Type: unit (create throttle over limit -> 429, shared with POST /links).
// Scenario: with link_create_rate_limit = 2 from one loopback-admin peer, the
//   first two link.create envelopes succeed and the third is 429 rate_limited
//   (fixed body); the blocked create never reaches the store.
// Boundaries: the throttle runs before execute via the shared
//   apply_create_throttle; the (N+1)th request is rejected.
// On failure first check: did the generic endpoint stop sharing the throttle,
//   or did the over-limit decision stop mapping to 429?
BOOST_AUTO_TEST_CASE(create_throttle_over_limit_returns_429)
{
    ManualClock clock {std::chrono::system_clock::time_point {}};
    auto limiter = std::make_shared<InMemoryRateLimiter>(clock);
    ServerConfig config = make_config(true, "admin");
    config.link_create_rate_limit = 2;
    config.link_create_rate_window_seconds = 60;
    ScopedCreateRuntime guard {config, limiter};

    for (int i = 0; i < 2; ++i) {
        const auto res = dispatch(
            make_request("/api/v1/commands",
                         R"({"v":1,"command":"link.create",)"
                         R"("args":{"target_url":"https://example.com/t",)"
                         R"("slug":"c23t)" + std::to_string(i) + "\"}}"),
            guard.config, k_loopback);
        BOOST_TEST(res.result_int() == 200, "create " + std::to_string(i));
    }

    const auto blocked = dispatch(
        make_request("/api/v1/commands",
                     R"({"v":1,"command":"link.create",)"
                     R"("args":{"target_url":"https://example.com/t",)"
                     R"("slug":"c23t2"}})"),
        guard.config, k_loopback);
    BOOST_TEST(blocked.result_int() == 429);
    BOOST_TEST(error_code(blocked.body()) == "rate_limited");
    BOOST_TEST(!url_shortener::linkRepository().getBySlug("c23t2"));
}

// Type: unit (dry_run create counts toward the throttle, T-75).
// Scenario: with link_create_rate_limit = 1, a dry_run link.create consumes the
//   one slot, so a following dry_run create is 429 - a dry_run is not cheaper
//   for the SSRF/validator and counts like a real create.
// Boundaries: the throttle fires for link.create regardless of the dry_run
//   option (before execute).
// On failure first check: did dry_run stop counting toward the limit?
BOOST_AUTO_TEST_CASE(dry_run_create_counts_toward_throttle)
{
    ManualClock clock {std::chrono::system_clock::time_point {}};
    auto limiter = std::make_shared<InMemoryRateLimiter>(clock);
    ServerConfig config = make_config(true, "admin");
    config.link_create_rate_limit = 1;
    config.link_create_rate_window_seconds = 60;
    ScopedCreateRuntime guard {config, limiter};

    const std::string dry =
        R"({"v":1,"command":"link.create",)"
        R"("args":{"target_url":"https://example.com/dry","slug":"c23dry"},)"
        R"("options":{"dry_run":true}})";

    const auto first =
        dispatch(make_request("/api/v1/commands", dry), guard.config,
                 k_loopback);
    BOOST_TEST(first.result_int() != 429);  // slot consumed, not blocked

    const auto second =
        dispatch(make_request("/api/v1/commands", dry), guard.config,
                 k_loopback);
    BOOST_TEST(second.result_int() == 429);
    BOOST_TEST(error_code(second.body()) == "rate_limited");
}

// Type: unit (throttle fail-open, disable switch, key is the peer; list is not
//   throttled).
// Scenario: a failing limiter lets a create through (fail-open); limit 0 never
//   calls the limiter; the key is the "link_create:"-scoped peer, not a header;
//   and link.list never consults the limiter.
// Boundaries: fail-open on a limiter error; the disable switch skips the block;
//   only link.create is throttled.
// On failure first check: did an error-set deny become a 429, did limit 0 start
//   calling the limiter, or did link.list start consuming a slot?
BOOST_AUTO_TEST_CASE(throttle_fail_open_disable_key_and_list_untouched)
{
    // Distinct slugs per sub-block so a create persisted by one block cannot
    // collide with another (the global store survives between blocks).
    const auto create_for = [](const std::string& slug) {
        return R"({"v":1,"command":"link.create","args":{)"
               R"("target_url":"https://example.com/k","slug":")"
            + slug + "\"}}";
    };

    // Fail-open: an error-set deny lets the create proceed.
    {
        ServerConfig config = make_config(true, "admin");
        config.link_create_rate_limit = 1;
        ScopedCreateRuntime guard {config,
                                   std::make_shared<FailingRateLimiter>()};
        const auto res =
            dispatch(make_request("/api/v1/commands", create_for("c23k1")),
                     guard.config, k_loopback);
        BOOST_TEST(res.result_int() == 200);
    }

    // Disable switch: limit 0 never calls the (always-deny) limiter.
    {
        auto limiter = std::make_shared<AlwaysDenyRateLimiter>();
        ServerConfig config = make_config(true, "admin");
        config.link_create_rate_limit = 0;
        ScopedCreateRuntime guard {config, limiter};
        const auto res =
            dispatch(make_request("/api/v1/commands", create_for("c23k2")),
                     guard.config, k_loopback);
        BOOST_TEST(res.result_int() == 200);
        BOOST_TEST(limiter->calls == 0);
    }

    // Key is the "link_create:"-scoped peer, ignoring X-Forwarded-For.
    {
        auto limiter = std::make_shared<CapturingRateLimiter>();
        ServerConfig config = make_config(true, "admin");
        config.link_create_rate_limit = 5;
        ScopedCreateRuntime guard {config, limiter};
        auto req = make_request("/api/v1/commands", create_for("c23k3"));
        req.set("X-Forwarded-For", "203.0.113.9");
        dispatch(req, guard.config, k_remote);
        BOOST_TEST(limiter->calls == 1);
        BOOST_TEST(limiter->last_key == "link_create:10.0.0.5");
    }

    // link.list is not throttled: the limiter is never consulted.
    {
        auto limiter = std::make_shared<CapturingRateLimiter>();
        ServerConfig config = make_config(true, "admin");
        config.link_create_rate_limit = 5;
        ScopedCreateRuntime guard {config, limiter};
        dispatch(make_request("/api/v1/commands",
                              R"({"v":1,"command":"link.list","args":{}})"),
                 guard.config, k_loopback);
        BOOST_TEST(limiter->calls == 0);
    }
}

// Type: unit (metric label of the path).
// Scenario: the target /api/v1/commands classifies as the api_commands metric
//   label (subtask 09).
// Boundaries: the classifier must agree with the registered route label.
// On failure first check: did routeLabelForTarget lose the api_commands branch?
BOOST_AUTO_TEST_CASE(metric_label_is_api_commands)
{
    BOOST_TEST(url_shortener::http::routeLabelForTarget("/api/v1/commands")
               == "api_commands");
}
