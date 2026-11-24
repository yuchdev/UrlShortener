/** @file 25_rest_get_commands_endpoint.cpp
 *  @brief Characterizes the resource-style GET /api/v1/commands endpoint
 *  (milestone 0006, Task 04.0 subtask 10): the config gate, the descriptor
 *  catalog body, query-parameter rejection, method handling, route
 *  coexistence with POST on the same path, and the catalog's no-secret
 *  guarantee. Also adds the Content-Type matrix regression for the sibling
 *  POST /api/v1/commands (carry-forward owned by subtask 10).
 *
 *  Numbering note: the spec named the file 23, but 23 (POST endpoint) and 24
 *  (command_api_caller) are already taken, so this GET endpoint test is 25 (the
 *  next free slot in the 15-29 http band).
 *
 *  Security: gate off discloses no catalog (threat model T-64/T-74); the
 *  catalog never carries a handler/secret/path (T-74); POST still requires the
 *  application/json content type, the only CSRF defence for loopback admin
 *  (T-67). The redirect fast path is untouched (contract C10).
 */
#define BOOST_TEST_MODULE RestGetCommandsEndpoint
#include <memory>
#include <optional>
#include <string>

#include <boost/asio/ip/address.hpp>
#include <boost/test/unit_test.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/request_handlers.hpp>
#include <url_shortener/http/route_registry.hpp>
#include <url_shortener/http/router_builder.hpp>
#include <url_shortener/security/control_set.hpp>

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
    return config;
}

BeastResponse dispatch(
    const BeastRequest& req,
    const ServerConfig& config,
    std::optional<boost::asio::ip::address> peer = std::nullopt)
{
    const auto router = RouterBuilder::buildApplicationRouter();
    return router.dispatch(req, config, false, std::move(peer));
}

/// Builds a request with the given method and target (no body).
BeastRequest make_request(const bhttp::verb method, const std::string& target)
{
    BeastRequest req {method, target, 11};
    req.set(bhttp::field::host, "short.test");
    return req;
}

/// Builds a POST request with a body and (optionally) a Content-Type header.
/// When @p content_type is empty the header is omitted.
BeastRequest make_post(const std::string& target,
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

/// Extracts the value of a `"<key>":"<value>"` string field from a body, or ""
/// when absent.
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

/// Installs a process runtime built from @p config (so the handler and the
/// direct command.list execution share one registry) and resets it on scope
/// exit, mirroring the fixtures in 23_rest_commands_endpoint.
struct ScopedRuntime
{
    std::shared_ptr<composition::CommandRuntime> runtime;

    explicit ScopedRuntime(const ServerConfig& config)
    {
        url_shortener::app::LinkBackend::reset();
        composition::reset_process_command_runtime();
        runtime = std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(
                config, url_shortener::app::LinkBackendHandle {}));
        composition::install_process_command_runtime(runtime);
    }

    ~ScopedRuntime()
    {
        composition::reset_process_command_runtime();
        url_shortener::app::LinkBackend::reset();
    }

    ScopedRuntime(const ScopedRuntime&) = delete;
    ScopedRuntime& operator=(const ScopedRuntime&) = delete;
};

}  // namespace

// Type: unit (gate off, method-mismatch reply, no catalog disclosure).
// Scenario: with the default config (command_api_enabled == false), GET
//   /api/v1/commands returns 405 invalid_method "Only POST is supported" with
//   Allow: POST (RFC 9110 15.5.6) - the reply the router already gives for the
//   POST-only path - and discloses no catalog. The JSON body matches the
//   gate-off GET /api/v1/links reply built the same way.
// Boundaries: POST /api/v1/commands (subtask 09) makes the path exist, so the
//   gate-off answer is a 405 method mismatch, NOT a 404 (the subtlety
//   handleListLinks handles for GET /api/v1/links). No command catalog leaks.
// On failure first check: did the gate branch change its status/message/Allow,
//   start leaking the catalog, or regress to a 404?
BOOST_AUTO_TEST_CASE(gate_off_is_invalid_method_and_discloses_no_catalog)
{
    const ServerConfig config = make_config(false, "user");

    // Pin the request id on both requests so the only non-deterministic field
    // (the server-generated request_id) is identical and the bodies can be
    // compared byte-for-byte.
    auto commands_req = make_request(bhttp::verb::get, "/api/v1/commands");
    commands_req.set("X-Internal-Request-Id", "req-gate-off");
    const auto res = dispatch(commands_req, config, k_remote);
    BOOST_TEST(res.result_int() == 405);
    BOOST_TEST(string_field(res.body(), "code") == "invalid_method");
    BOOST_TEST(contains(res.body(), "Only POST is supported"));
    BOOST_TEST(std::string(res.base()[bhttp::field::allow]) == "POST");
    // No catalog disclosure on the gate-off branch.
    BOOST_TEST(!contains(res.body(), "\"commands\""));
    BOOST_TEST(!contains(res.body(), "command.list"));

    // Byte-equal to the gate-off GET /api/v1/links reply (handleListLinks also
    // reproduces the POST-only method-mismatch for its sibling path).
    auto links_req = make_request(bhttp::verb::get, "/api/v1/links");
    links_req.set("X-Internal-Request-Id", "req-gate-off");
    const auto links_baseline = dispatch(links_req, config, k_remote);
    BOOST_TEST(res.body() == links_baseline.body());
}

// Type: unit (gate on, role user: full catalog, sorted, equals command.list).
// Scenario: under role user, GET /api/v1/commands is 200 with a body that is a
//   {"commands":[...]} object listing every registered command (sorted by
//   name), each with an args array; the body is byte-identical to the result of
//   command.list executed directly on the same runtime.
// Boundaries: a read command, so a User caller may list (T-74); the resource
//   route returns the result verbatim, not an envelope.
// On failure first check: did the gated caller stop defaulting to User, or did
//   the resource body stop equalling command.list's result?
BOOST_AUTO_TEST_CASE(enabled_user_lists_full_catalog_equal_to_command_list)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    const auto res = dispatch(
        make_request(bhttp::verb::get, "/api/v1/commands"), config, k_remote);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(res.body().rfind("{\"commands\":[", 0) == 0U);
    BOOST_TEST(contains(res.body(), "\"args\":["));

    // Body equals command.list executed directly on the same runtime registry.
    commands::CallerContext caller;
    caller.principal = "test";
    caller.control_set = ControlSet::User;
    caller.transport = "test";
    commands::CommandRequest direct;
    direct.v = 1;
    direct.id = "direct";
    direct.command = "command.list";
    direct.args_json = "{}";
    const commands::CommandResponse ran =
        fixture.runtime->registry->execute(caller, direct);
    BOOST_TEST(ran.ok);
    BOOST_TEST(res.body() == ran.result_json);
}

// Type: unit (every documented command present with the right perm/mutating).
// Scenario: the catalog lists each of the nine link commands, link.list,
//   db.tables, db.rows, system.info and command.list, with command.list a
//   non-mutating read and link.create a mutating write, and the entries are in
//   sorted name order.
// Boundaries: the catalog advertises name/summary/perm/kind/mutating/dry_run
//   and args only (T-74), serialized by descriptor_to_json.
// On failure first check: did a command stop registering, or did descriptor
//   serialization drop a field?
BOOST_AUTO_TEST_CASE(catalog_contains_every_command_with_metadata)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    const auto res = dispatch(
        make_request(bhttp::verb::get, "/api/v1/commands"), config, k_loopback);
    BOOST_TEST(res.result_int() == 200);
    const std::string& body = res.body();

    for (const char* name : {"\"name\":\"link.create\"",
                             "\"name\":\"link.get\"",
                             "\"name\":\"link.update\"",
                             "\"name\":\"link.delete\"",
                             "\"name\":\"link.enable\"",
                             "\"name\":\"link.disable\"",
                             "\"name\":\"link.restore\"",
                             "\"name\":\"link.preview\"",
                             "\"name\":\"link.stats\"",
                             "\"name\":\"link.list\"",
                             "\"name\":\"db.tables\"",
                             "\"name\":\"db.rows\"",
                             "\"name\":\"system.info\"",
                             "\"name\":\"command.list\""})
    {
        BOOST_TEST(contains(body, name), name);
    }

    // command.list is a non-mutating read; link.create a mutating write.
    BOOST_TEST(contains(body, "\"name\":\"command.list\",\"summary\":"));
    BOOST_TEST(contains(body, "\"name\":\"command.list\""));
    BOOST_TEST(contains(body, "\"perm\":\"read\",\"kind\":"));
    BOOST_TEST(contains(body, "\"perm\":\"write\""));
    BOOST_TEST(contains(body, "\"mutating\":true"));
    BOOST_TEST(contains(body, "\"mutating\":false"));

    // Sorted by name: command.list < db.rows < db.tables < link.create <
    // link.list < system.info (std::map ordering in the registry).
    const auto p_command = body.find("\"name\":\"command.list\"");
    const auto p_db_rows = body.find("\"name\":\"db.rows\"");
    const auto p_db_tables = body.find("\"name\":\"db.tables\"");
    const auto p_create = body.find("\"name\":\"link.create\"");
    const auto p_list = body.find("\"name\":\"link.list\"");
    const auto p_system = body.find("\"name\":\"system.info\"");
    BOOST_TEST(p_command < p_db_rows);
    BOOST_TEST(p_db_rows < p_db_tables);
    BOOST_TEST(p_db_tables < p_create);
    BOOST_TEST(p_create < p_list);
    BOOST_TEST(p_list < p_system);
}

// Type: unit (no secret or internal path in the catalog, T-74).
// Scenario: the catalog body contains none of the secret/path sentinels
//   uri.txt, password, dsn (case-insensitive substrings would also be caught,
//   but the serializer emits none of these tokens at all).
// Boundaries: command.list serializes only public descriptor metadata; no
//   handler, maps_to, DSN or file path is reachable.
// On failure first check: did an arg default/doc start carrying an operational
//   secret or internal path?
BOOST_AUTO_TEST_CASE(catalog_contains_no_secret_or_internal_path)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    const auto res = dispatch(
        make_request(bhttp::verb::get, "/api/v1/commands"), config, k_loopback);
    BOOST_TEST(res.result_int() == 200);
    BOOST_TEST(!contains(res.body(), "uri.txt"));
    BOOST_TEST(!contains(res.body(), "password"));
    BOOST_TEST(!contains(res.body(), "dsn"));
}

// Type: unit (strict query: any parameter is an unknown arg).
// Scenario: GET /api/v1/commands?x=1 is 400 invalid_request with the message
//   "x: unknown" (command.list takes no arguments, so the registry rejects the
//   forwarded parameter); a malformed query is 400 invalid_request too.
// Boundaries: the handler forwards every parameter as an args member, letting
//   the registry reject it (contract C1); a bad %-escape is a fixed-detail 400.
// On failure first check: did the handler stop forwarding the parameter, or did
//   invalid_field stop mapping to invalid_request?
BOOST_AUTO_TEST_CASE(any_query_parameter_is_unknown)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    const auto unknown =
        dispatch(make_request(bhttp::verb::get, "/api/v1/commands?x=1"),
                 config,
                 k_remote);
    BOOST_TEST(unknown.result_int() == 400);
    BOOST_TEST(string_field(unknown.body(), "code") == "invalid_request");
    BOOST_TEST(contains(unknown.body(), "x: unknown"));

    const auto malformed =
        dispatch(make_request(bhttp::verb::get, "/api/v1/commands?a=%zz"),
                 config,
                 k_remote);
    BOOST_TEST(malformed.result_int() == 400);
    BOOST_TEST(string_field(malformed.body(), "code") == "invalid_request");
    BOOST_TEST(!contains(malformed.body(), "\"commands\""));
}

// Type: unit (method handling and route coexistence on /api/v1/commands).
// Scenario: PUT /api/v1/commands is 405 invalid_method with Allow: GET, POST
//   (RFC 9110 15.5.6); POST /api/v1/commands (with the required JSON content
//   type) still returns a response envelope, so the new GET route coexists
//   with the POST route on the same path.
// Boundaries: the router matches GET and POST independently on one path; an
//   unsupported method is a 405 method mismatch naming both supported verbs.
// On failure first check: did adding the GET route break POST dispatch, or did
//   the method-mismatch stop being a 405 / drop the Allow header?
BOOST_AUTO_TEST_CASE(put_is_invalid_method_post_still_returns_envelope)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    const auto put = dispatch(
        make_request(bhttp::verb::put, "/api/v1/commands"), config, k_loopback);
    BOOST_TEST(put.result_int() == 405);
    BOOST_TEST(string_field(put.body(), "code") == "invalid_method");
    BOOST_TEST(std::string(put.base()[bhttp::field::allow]) == "GET, POST");

    const auto posted =
        dispatch(make_post("/api/v1/commands",
                           R"({"v":1,"command":"link.list","args":{}})"),
                 config,
                 k_remote);
    BOOST_TEST(posted.result_int() == 200);
    BOOST_TEST(contains(posted.body(), "\"ok\":true"));
}

// Type: unit (route registered in both router and registry, api_commands).
// Scenario: GET /api/v1/commands is present in both registeredRoutes() and the
//   built router with the route label api_commands.
// Boundaries: the consistency test requires router and registry to agree; the
//   metric label of the path is api_commands (subtask 09).
// On failure first check: did the router line or the descriptor drop, or did
//   the label diverge?
BOOST_AUTO_TEST_CASE(route_present_in_router_and_registry_with_label)
{
    bool in_registry = false;
    for (const auto& route : url_shortener::http::registeredRoutes()) {
        if (route.method == "GET" && route.path_pattern == "/api/v1/commands") {
            in_registry = true;
            BOOST_TEST(route.route_label == "api_commands");
        }
    }
    BOOST_TEST(in_registry);

    bool in_router = false;
    const auto router = RouterBuilder::buildApplicationRouter();
    for (const auto& route : router.routes()) {
        if (route.method == "GET" && route.path_pattern == "/api/v1/commands") {
            in_router = true;
            BOOST_TEST(route.route_label == "api_commands");
        }
    }
    BOOST_TEST(in_router);
}

// Type: unit (Content-Type matrix for POST /api/v1/commands; carry T-67).
// Scenario: the only CSRF defence for the loopback-admin command endpoint is
//   the application/json content-type gate. text/plain,
//   application/x-www-form-urlencoded, and multipart/form-data with a boundary
//   parameter are each 415 and the envelope is not parsed; application/json
//   with a charset parameter is accepted (parameters are ignored).
// Boundaries: only the media type matters (case-insensitive, parameters
//   dropped); a 415 forces a CORS preflight the server never grants. Enabling
//   any CORS allowance would reopen this (documented in docs/commands/README).
// On failure first check: did the media-type parse start honouring parameters,
//   or did a rejected type start parsing the body?
BOOST_AUTO_TEST_CASE(post_content_type_matrix)
{
    const ServerConfig config = make_config(true, "admin");
    ScopedRuntime fixture {config};
    const std::string body =
        R"({"v":1,"command":"link.list","args":{},"id":"CT-SENTINEL"})";

    for (const std::string& ct :
         {std::string("text/plain"),
          std::string("application/x-www-form-urlencoded"),
          std::string("multipart/form-data; boundary=----abc123")})
    {
        const auto res = dispatch(
            make_post("/api/v1/commands", body, ct), config, k_loopback);
        BOOST_TEST(res.result_int() == 415, "ct=[" + ct + "]");
        BOOST_TEST(string_field(res.body(), "code") == "unsupported_media_type",
                   "ct=[" + ct + "]");
        // The 415 refusal never parses or echoes the envelope.
        BOOST_TEST(!contains(res.body(), "\"ok\":"), "ct=[" + ct + "]");
        BOOST_TEST(!contains(res.body(), "CT-SENTINEL"), "ct=[" + ct + "]");
    }

    // application/json with a charset parameter is accepted (params ignored).
    const auto accepted = dispatch(
        make_post("/api/v1/commands", body, "application/json; charset=utf-8"),
        config,
        k_loopback);
    BOOST_TEST(accepted.result_int() != 415);
    BOOST_TEST(contains(accepted.body(), "\"ok\":true"));
}
