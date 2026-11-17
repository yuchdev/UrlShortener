/** @file 40_jobs_routes.cpp
 *  @brief Characterizes the REST job routes (milestone 0006, Task 05.0 subtask
 *  11): GET /api/v1/jobs, GET /api/v1/jobs/{id} and POST /api/v1/jobs/{id}/cancel
 *  as thin projections onto job.list / job.get / job.cancel. Covers the config
 *  gate, route registration, path/query arg mapping, status mapping, the role
 *  model (read for any caller, cancel only for loopback admin), process-wide job
 *  visibility across requests, and the method matrix.
 *
 *  Security: these routes are new HTTP surface on an unauthenticated server and
 *  a job result can inline a full link-table export, so they are gated exactly
 *  like the generic endpoint (threat model T-64): disabled -> byte-equal to an
 *  unmatched route; enabled -> command_api_caller(config, peer), write (cancel)
 *  only for a loopback admin (T-92). Jobs carry no owner in V1; ids are
 *  correlation handles only (T-93). The redirect fast path is untouched (C10).
 */
#define BOOST_TEST_MODULE JobsRestRoutes
#include <boost/test/unit_test.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include <boost/asio/ip/address.hpp>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/job_runner.hpp>
#include <url_shortener/commands/job_types.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/job_runtime.hpp>
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

BeastResponse dispatch(const BeastRequest& req,
                       const ServerConfig& config,
                       std::optional<boost::asio::ip::address> peer =
                           std::nullopt)
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

/// Builds a POST request with a body and a Content-Type header.
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

/// Extracts the value of a top-level `"<key>":"<value>"` string field.
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

/// Extracts the nested job id from a serialized response envelope's
/// `"job":{"id":"j-N",...}` payload, or "" when absent.
std::string job_id_field(const std::string& body)
{
    const std::string needle = "\"job\":{\"id\":\"";
    const auto pos = body.find(needle);
    if (pos == std::string::npos) {
        return {};
    }
    const auto start = pos + needle.size();
    const auto end = body.find('"', start);
    return end == std::string::npos ? std::string {}
                                    : body.substr(start, end - start);
}

/// Installs a process runtime (and a fresh process-wide JobRunner) built from
/// @p config so the handlers and any directly-submitted job share one runtime,
/// and tears both down on scope exit.
struct ScopedRuntime
{
    std::shared_ptr<composition::CommandRuntime> runtime;

    explicit ScopedRuntime(const ServerConfig& config)
    {
        url_shortener::app::LinkBackend::reset();
        composition::reset_process_command_runtime();
        composition::reset_process_job_runtime_for_tests();
        runtime = std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(
                config, url_shortener::app::LinkBackendHandle {}));
        composition::install_process_command_runtime(runtime);
    }

    ~ScopedRuntime()
    {
        composition::reset_process_command_runtime();
        composition::reset_process_job_runtime_for_tests();
        url_shortener::app::LinkBackend::reset();
    }

    ScopedRuntime(const ScopedRuntime&) = delete;
    ScopedRuntime& operator=(const ScopedRuntime&) = delete;
};

/// Submits a long-lived cooperative job on the runtime's process-wide runner
/// and returns its id. The body spins until cancellation is requested (so the
/// runner's shutdown on teardown always joins) or @p gate is released, letting a
/// test observe a non-terminal job while it probes the cancel route.
std::string submit_running_job(ScopedRuntime& fixture,
                               std::shared_ptr<std::atomic<bool>> gate)
{
    commands::JobBody body = [gate](commands::IJobContext& ctx) {
        while (!gate->load() && !ctx.cancel_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        commands::JobOutcome outcome;
        outcome.cancelled = ctx.cancel_requested();
        outcome.ok = !outcome.cancelled;
        return outcome;
    };
    const commands::JobSubmitResult result = fixture.runtime->job_runner->submit(
        "link.export", "0123456789abcdef", std::move(body));
    BOOST_REQUIRE(result.job.has_value());
    return result.job->id;
}

bool job_is_terminal(ScopedRuntime& fixture, const std::string& id)
{
    const std::optional<commands::JobRecord> record =
        fixture.runtime->job_runner->get(id);
    return record.has_value() && commands::is_terminal(record->state);
}

}  // namespace

// Type: unit (gate off, byte-equal to an unmatched route, no job touched).
// Scenario: with the default config (command_api_enabled == false), all three
//   job routes return exactly the unmatched-route 404 body ("Route not found")
//   and a job already submitted on the runner is left untouched by the gate-off
//   cancel route.
// Boundaries: the gate is the whole perimeter (threat model T-64); off means
//   "route does not exist", byte-identical to the router's own /api/* 404.
// On failure first check: did a gate branch change its status/message, start
//   reaching the runner, or regress from the shared 404?
BOOST_AUTO_TEST_CASE(gate_off_three_routes_byte_equal_and_no_job_touched)
{
    const ServerConfig config = make_config(false, "admin");
    ScopedRuntime fixture {config};

    auto gate = std::make_shared<std::atomic<bool>>(false);
    const std::string id = submit_running_job(fixture, gate);

    // The unmatched-route baseline: a reserved /api/ path with no route.
    auto baseline_req = make_request(bhttp::verb::get, "/api/v1/zzz-no-route");
    baseline_req.set("X-Internal-Request-Id", "req-gate-off");
    const auto baseline = dispatch(baseline_req, config, k_loopback);
    BOOST_TEST(baseline.result_int() == 404);
    BOOST_TEST(string_field(baseline.body(), "code") == "not_found");

    struct Probe
    {
        bhttp::verb method;
        std::string target;
    };
    for (const Probe& probe :
         {Probe {bhttp::verb::get, "/api/v1/jobs?state=running"},
          Probe {bhttp::verb::get, "/api/v1/jobs/" + id},
          Probe {bhttp::verb::post, "/api/v1/jobs/" + id + "/cancel"}}) {
        BeastRequest req {probe.method, probe.target, 11};
        req.set(bhttp::field::host, "short.test");
        req.set("X-Internal-Request-Id", "req-gate-off");
        const auto res = dispatch(req, config, k_loopback);
        BOOST_TEST(res.result_int() == 404, probe.target);
        BOOST_TEST(res.body() == baseline.body(), probe.target);
    }

    // The gate-off cancel never reached job.cancel: the job still runs.
    BOOST_TEST(!job_is_terminal(fixture, id));
    gate->store(true);
}

// Type: unit (routes registered in router and registry with method and label).
// Scenario: the three job routes appear in both registeredRoutes() and the
//   built router with the api_commands label and correct methods.
// Boundaries: the consistency guard requires router and registry to agree; the
//   metric label of /api/v1/jobs is api_commands (subtask 09).
// On failure first check: did a router line or descriptor drop, or did the
//   label diverge?
BOOST_AUTO_TEST_CASE(routes_present_in_registry_and_router)
{
    struct Row
    {
        std::string method;
        std::string path;
    };
    const Row rows[] = {{"GET", "/api/v1/jobs"},
                        {"GET", "/api/v1/jobs/{id}"},
                        {"POST", "/api/v1/jobs/{id}/cancel"}};

    for (const Row& row : rows) {
        bool in_registry = false;
        for (const auto& route : url_shortener::http::registeredRoutes()) {
            if (route.method == row.method
                && route.path_pattern == row.path) {
                in_registry = true;
                BOOST_TEST(route.route_label == "api_commands", row.path);
            }
        }
        BOOST_TEST(in_registry, row.method + " " + row.path);

        bool in_router = false;
        const auto router = RouterBuilder::buildApplicationRouter();
        for (const auto& route : router.routes()) {
            if (route.method == row.method
                && route.path_pattern == row.path) {
                in_router = true;
                BOOST_TEST(route.route_label == "api_commands", row.path);
            }
        }
        BOOST_TEST(in_router, row.method + " " + row.path);
    }
}

// Type: unit (query -> job.list args; returns the summary page).
// Scenario: GET /api/v1/jobs?state=running&limit=10 is 200 and the body is the
//   job.list result page ({"items":[...],"total":n}); an unfiltered GET also
//   lists.
// Boundaries: offset/limit are forwarded as JSON number tokens, state as a
//   string the enum arg validates; the handler only assembles args (contract
//   C1).
// On failure first check: did the handler stop forwarding a parameter, or did
//   job.list stop returning the item/total page?
BOOST_AUTO_TEST_CASE(list_jobs_maps_query_to_job_list)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    const auto filtered = dispatch(
        make_request(bhttp::verb::get, "/api/v1/jobs?state=running&limit=10"),
        config, k_remote);
    BOOST_TEST(filtered.result_int() == 200);
    BOOST_TEST(filtered.body().rfind("{\"items\":[", 0) == 0U);
    BOOST_TEST(contains(filtered.body(), "\"total\":"));

    const auto all =
        dispatch(make_request(bhttp::verb::get, "/api/v1/jobs"), config,
                 k_remote);
    BOOST_TEST(all.result_int() == 200);
    BOOST_TEST(contains(all.body(), "\"items\":["));
}

// Type: unit (path id -> job.get; unknown id -> 404).
// Scenario: a job submitted on the runner is returned by GET /api/v1/jobs/{id}
//   (200, body carries its id/command/state); an unknown id is 404 not_found.
// Boundaries: the path id is the sole job.get arg; a miss maps to not_found via
//   the bridge (the raw id is never echoed, contract C6).
// On failure first check: did path-param extraction break, or did not_found
//   stop mapping to 404?
BOOST_AUTO_TEST_CASE(get_job_by_path_id_and_unknown_is_404)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    auto gate = std::make_shared<std::atomic<bool>>(false);
    const std::string id = submit_running_job(fixture, gate);

    const auto hit =
        dispatch(make_request(bhttp::verb::get, "/api/v1/jobs/" + id), config,
                 k_remote);
    BOOST_TEST(hit.result_int() == 200);
    BOOST_TEST(contains(hit.body(), "\"id\":\"" + id + "\""));
    BOOST_TEST(contains(hit.body(), "\"command\":\"link.export\""));

    const auto miss =
        dispatch(make_request(bhttp::verb::get, "/api/v1/jobs/j-does-not-exist"),
                 config, k_remote);
    BOOST_TEST(miss.result_int() == 404);
    BOOST_TEST(string_field(miss.body(), "code") == "not_found");

    gate->store(true);
}

// Type: unit (invalid query -> 400).
// Scenario: GET /api/v1/jobs?limit=0 and ?state=bogus are each 400
//   invalid_request (the registry rejects the out-of-range bound and the
//   unknown enum token).
// Boundaries: the handler forwards the value; job.list's ArgSpec bounds/enum
//   reject it (invalid_field -> invalid_request via the bridge).
// On failure first check: did integer tokenization change, or did invalid_field
//   stop mapping to invalid_request?
BOOST_AUTO_TEST_CASE(bad_limit_and_bad_state_are_400)
{
    const ServerConfig config = make_config(true, "user");
    ScopedRuntime fixture {config};

    const auto bad_limit =
        dispatch(make_request(bhttp::verb::get, "/api/v1/jobs?limit=0"), config,
                 k_remote);
    BOOST_TEST(bad_limit.result_int() == 400);
    BOOST_TEST(string_field(bad_limit.body(), "code") == "invalid_request");

    const auto bad_state =
        dispatch(make_request(bhttp::verb::get, "/api/v1/jobs?state=bogus"),
                 config, k_remote);
    BOOST_TEST(bad_state.result_int() == 400);
    BOOST_TEST(string_field(bad_state.body(), "code") == "invalid_request");
}

// Type: unit (role model: read any, cancel loopback-admin only; T-92).
// Scenario: role user (any peer) reads jobs (200) but a cancel is 403 and the
//   job keeps running; role admin from a non-loopback peer is also 403; role
//   admin from 127.0.0.1 cancels with 200.
// Boundaries: job.cancel is write/mutating, so execute denies User before the
//   handler; the admin escalation needs role admin AND a loopback peer
//   (command_api_caller); the resource-route Admin caller is never used here.
// On failure first check: did the gated caller stop requiring loopback for
//   admin, or did a denied cancel start touching the job?
BOOST_AUTO_TEST_CASE(cancel_role_model_user_denied_admin_loopback_allowed)
{
    // role user: GET ok, cancel denied, job keeps running.
    {
        const ServerConfig config = make_config(true, "user");
        ScopedRuntime fixture {config};
        auto gate = std::make_shared<std::atomic<bool>>(false);
        const std::string id = submit_running_job(fixture, gate);

        const auto read =
            dispatch(make_request(bhttp::verb::get, "/api/v1/jobs/" + id),
                     config, k_loopback);
        BOOST_TEST(read.result_int() == 200);

        const auto denied = dispatch(
            make_post("/api/v1/jobs/" + id + "/cancel", ""), config, k_loopback);
        BOOST_TEST(denied.result_int() == 403);
        BOOST_TEST(string_field(denied.body(), "code") == "not_authorized");
        BOOST_TEST(!job_is_terminal(fixture, id));
        gate->store(true);
    }

    // role admin from a non-loopback peer: cancel still 403.
    {
        const ServerConfig config = make_config(true, "admin");
        ScopedRuntime fixture {config};
        auto gate = std::make_shared<std::atomic<bool>>(false);
        const std::string id = submit_running_job(fixture, gate);

        const auto denied = dispatch(
            make_post("/api/v1/jobs/" + id + "/cancel", ""), config, k_remote);
        BOOST_TEST(denied.result_int() == 403);
        BOOST_TEST(string_field(denied.body(), "code") == "not_authorized");
        BOOST_TEST(!job_is_terminal(fixture, id));
        gate->store(true);
    }

    // role admin from 127.0.0.1: cancel succeeds (200).
    {
        const ServerConfig config = make_config(true, "admin");
        ScopedRuntime fixture {config};
        auto gate = std::make_shared<std::atomic<bool>>(false);
        const std::string id = submit_running_job(fixture, gate);

        const auto ok = dispatch(
            make_post("/api/v1/jobs/" + id + "/cancel", ""), config,
            k_loopback);
        BOOST_TEST(ok.result_int() == 200);
        BOOST_TEST(contains(ok.body(), "\"id\":\"" + id + "\""));
        gate->store(true);
    }
}

// Type: unit (process-wide runner: job submitted in one request, read in a
//   later one).
// Scenario: POST /api/v1/commands runs link.export (a job command) and returns a
//   job envelope; a later GET /api/v1/jobs/{id} on a freshly built router
//   returns that job record (the runner is process-wide, not per-request).
// Boundaries: every per-request runtime shares one JobRunner (subtask 20); the
//   id is a correlation handle (T-93).
// On failure first check: did the runtime stop sharing the process runner, or
//   did the job envelope stop carrying the id?
BOOST_AUTO_TEST_CASE(job_submitted_via_post_commands_visible_via_get_later)
{
    const ServerConfig config = make_config(true, "admin");
    ScopedRuntime fixture {config};

    const auto submitted = dispatch(
        make_post("/api/v1/commands",
                  R"({"v":1,"command":"link.export","args":{"format":"csv"}})"),
        config, k_loopback);
    BOOST_TEST(submitted.result_int() == 200);
    const std::string id = job_id_field(submitted.body());
    BOOST_TEST(!id.empty());

    // A later request through a newly built router still sees the job.
    const auto fetched =
        dispatch(make_request(bhttp::verb::get, "/api/v1/jobs/" + id), config,
                 k_loopback);
    BOOST_TEST(fetched.result_int() == 200);
    BOOST_TEST(contains(fetched.body(), "\"id\":\"" + id + "\""));
    BOOST_TEST(contains(fetched.body(), "\"command\":\"link.export\""));
}

// Type: unit (method matrix: an unsupported method on a job path is 400).
// Scenario: DELETE /api/v1/jobs/{id} matches the GET-only path, so the router
//   answers 400 invalid_method "Only GET is supported" (the existing matrix
//   behavior), regardless of the gate.
// Boundaries: the router decides method mismatch before any handler runs, so
//   the gate is never consulted for an unsupported method.
// On failure first check: did adding the GET route change the method-mismatch
//   reply, or did a new verb get registered on the path?
BOOST_AUTO_TEST_CASE(delete_job_is_invalid_method)
{
    const ServerConfig config = make_config(true, "admin");
    ScopedRuntime fixture {config};

    const auto res =
        dispatch(make_request(bhttp::verb::delete_, "/api/v1/jobs/j-1"), config,
                 k_loopback);
    BOOST_TEST(res.result_int() == 400);
    BOOST_TEST(string_field(res.body(), "code") == "invalid_method");
    BOOST_TEST(contains(res.body(), "Only GET is supported"));
}
