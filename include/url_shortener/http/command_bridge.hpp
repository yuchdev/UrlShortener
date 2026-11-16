#pragma once

#include <optional>
#include <string>
#include <string_view>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/http/handler_types.hpp>

// The single bridge between the legacy REST resource routes and the unified
// command model (milestone 0006 contract C1): the one place in `src/http/` that
// obtains a registry, runs a command on behalf of a resource route, and renders
// the `CommandResponse` back into the exact HTTP status, JSON body and error
// strings those routes return today (contract C3). It includes Beast only
// transitively (via `handler_types.hpp`) and pulls in no persistence-layer
// header at all, so the command layer stays free of transport-storage coupling
// (success criterion 3).

namespace url_shortener
{
namespace http
{

/**
 * @brief Builds the fixed caller the legacy REST resource routes execute as.
 *
 * Today's resource routes are unauthenticated and open: `src/http` performs no
 * `AccessGuard` check. To preserve that behavior byte-for-byte (contract C3)
 * every resource-route command runs as `ControlSet::Admin`. This is an
 * explicit, documented carry-over of the status quo (threat model T-60), not
 * new trust: Milestone 0002 replaces it with a session-derived caller. The
 * `principal` (`"rest-resource"`) and `transport` (`"rest"`) labels are fixed,
 * non-identifying constants and never carry a peer IP, token, or any request
 * value (threat model T-84), so they are safe to place in an audit line.
 *
 * @return A @ref commands::CallerContext with `principal == "rest-resource"`,
 * `control_set == ControlSet::Admin`, and `transport == "rest"`.
 */
commands::CallerContext resource_route_caller();

/**
 * @brief Builds the per-request command runtime a resource route executes
 * against.
 *
 * The runtime is built fresh for every request rather than cached across
 * requests: `makeCommandService`-style wiring captures the request's
 * `const ServerConfig&`, and the unit tests dispatch with different
 * `ServerConfig` objects (e.g. `shortener_allow_private_targets`), so a runtime
 * frozen around one config would change behavior (contract C3). This is the
 * management plane, not the redirect hot path (contract C10), so the per-request
 * cost of one ~15-descriptor registry is acceptable; caching per `HttpServer`
 * is a later optimization. The runtime wires `composition::build_command_runtime`
 * over the process-wide holders (`app::LinkBackend::store()`, the global click
 * repository and the published system-info snapshot) using an inactive
 * `app::LinkBackendHandle`, so link CRUD reaches the installed store exactly as
 * the resource routes do today; the process-wide `JobRunner` a later task
 * installs is shared, so per-request runtimes never lose jobs.
 *
 * @param config Server configuration the built runtime's link service borrows by
 * reference; it must outlive the returned runtime (the call site holds the
 * request's `ServerConfig`, which satisfies this).
 * @return A @ref composition::CommandRuntime owning the registry and every
 * object its handlers reference.
 * @throws std::runtime_error only on a startup-class wiring failure (propagated
 * from @ref composition::build_command_runtime); expected conditions never
 * throw.
 */
composition::CommandRuntime make_request_runtime(const ::ServerConfig& config);

/**
 * @brief REST transport policy applied by every REST entry point before
 * `execute`: refuse any command whose descriptor requires `manage_users`.
 *
 * A transport rule ("this transport never carries user management"), not a
 * permission check: it refuses a `manage_users` descriptor for every role and
 * every peer, including an `Admin` caller, until Milestone 0002 provides
 * session-derived callers (decision log D4, threat model T-61). Authorization of
 * every command it lets through still happens in @ref
 * commands::CommandRegistry::execute (contract C7). The descriptor's handler is
 * never entered on a refusal.
 *
 * @param registry The registry to resolve @p command against.
 * @param command The command name the request addresses.
 * @param id Correlation id to echo in the refusal envelope; a validated request
 * id, never raw hostile input.
 * @return A `not_authorized` failure response echoing @p id when @p command
 * resolves to a descriptor whose base `permission` is
 * @ref commands::Permission::manage_users; `std::nullopt` otherwise (an unknown
 * command, or any `read`/`write`/`migrate` descriptor), in which case the caller
 * proceeds to `execute`.
 */
std::optional<commands::CommandResponse> rest_policy_refusal(
    const commands::CommandRegistry& registry,
    std::string_view command,
    std::string_view id);

/**
 * @brief Maps a failed command's error to the legacy REST `error.code` string.
 *
 * Returns the 01.0/02 `legacy_rest_code` column for the error's code, except
 * that an `invalid_field` failure whose message is exactly
 * @ref commands::detail_invalid_timestamp or
 * @ref commands::detail_invalid_bucket is promoted to that message as the code,
 * mirroring the legacy `codeForAppError`. Only those two stats details are
 * promoted; every other `invalid_field` message (e.g. `range_too_large`,
 * `"slug: missing"`) keeps the generic `invalid_request` code.
 *
 * @param error The failed command's error; its `message` is compared only
 * against the two fixed promotion tokens, never echoed into a code.
 * @return The promoted detail token for a promotable `invalid_field`, otherwise
 * `error_info_for(error.code).legacy_rest_code`.
 */
std::string legacy_error_code(const commands::CommandError& error);

/**
 * @brief Renders a @ref commands::CommandResponse as the exact HTTP response a
 * legacy REST resource route returns today (contracts C1/C3).
 *
 * The single place in `src/http/` that maps a command outcome to an HTTP status
 * and body; handlers never switch on error codes (constraint C1). Outcomes:
 * - a success carrying a result: @p success_status, body
 *   `response.result_json` embedded verbatim, `Content-Type: application/json`;
 * - a success carrying a `job` (never produced by a resource route): `500`
 *   `internal_error`, because a resource route has no job-rendering path
 *   (threat model T-63);
 * - a failure: status `error_info_for(code).legacy_rest_status`, body built by
 *   `url_shortener::makeApiErrorResponse` with `code == legacy_error_code(error)`
 *   and `message == error.message`, so the `{"error":{...}}` envelope is
 *   byte-identical to today's.
 *
 * `error.message` is a fixed, value-free string from the command/service layer
 * (never a request value, contract C6), so it is safe to place in the body.
 *
 * @param req Incoming request; mirrors HTTP version and propagates the request
 * id into the body/headers via the response helpers.
 * @param config Server configuration passed to the response helpers.
 * @param is_tls `true` on HTTPS connections; gates HSTS/`nosniff`.
 * @param response The command outcome to render.
 * @param success_status HTTP status for a successful result (e.g. `200` for a
 * read, `201` for a create); ignored for failure and job outcomes.
 * @return The fully-constructed Beast response.
 */
BeastResponse resource_response(const BeastRequest& req,
                                const ::ServerConfig& config,
                                bool is_tls,
                                const commands::CommandResponse& response,
                                unsigned success_status = 200);

/**
 * @brief Runs one command for a resource route against an explicit registry
 * and caller (the seam unit tests inject a stub registry through).
 *
 * Applies @ref rest_policy_refusal first; on no refusal it builds a
 * `CommandRequest {v = 1, id = request_id, command, args_json, options = {}}`
 * directly (no envelope parsing) and returns
 * `registry.execute(caller, request)`. Authorization happens entirely inside
 * `execute` (contract C7); this function adds only the transport policy.
 *
 * @param registry The registry to execute against; borrowed for the call only.
 * @param command The command name to run.
 * @param args_json The exact `args` object JSON text (produced by
 * @ref commands::args_builder); moved into the request verbatim.
 * @param request_id Correlation id echoed in the response envelope; a validated
 * request id, never raw hostile input. Moved into the request.
 * @param caller The authenticated caller; `control_set` is the sole
 * authorization input.
 * @return The execution response, or the policy refusal when @p command requires
 * `manage_users`.
 */
commands::CommandResponse execute_resource_command(
    const commands::CommandRegistry& registry,
    std::string_view command,
    std::string args_json,
    std::string request_id,
    const commands::CallerContext& caller);

/**
 * @brief Runs one command for a resource route, as the legacy Admin caller.
 *
 * Builds the per-request runtime (@ref make_request_runtime) and delegates to
 * the explicit-registry overload with @ref resource_route_caller. The runtime
 * lives only for the call.
 *
 * @param config Server configuration; borrowed by the per-request runtime for
 * the duration of the call.
 * @param command The command name to run.
 * @param args_json The exact `args` object JSON text; moved into the request.
 * @param request_id Correlation id echoed in the response; moved into the
 * request.
 * @return The execution response, or a `manage_users` policy refusal.
 */
commands::CommandResponse execute_resource_command(const ::ServerConfig& config,
                                                   std::string_view command,
                                                   std::string args_json,
                                                   std::string request_id);

/**
 * @brief Runs one command for a resource route, with an explicit caller.
 *
 * As the three-argument overload, but with a caller the gated routes supply
 * (subtask 08). Builds the per-request runtime and delegates to the
 * explicit-registry overload.
 *
 * @param config Server configuration; borrowed by the per-request runtime for
 * the duration of the call.
 * @param command The command name to run.
 * @param args_json The exact `args` object JSON text; moved into the request.
 * @param request_id Correlation id echoed in the response; moved into the
 * request.
 * @param caller The authenticated caller; `control_set` is the sole
 * authorization input.
 * @return The execution response, or a `manage_users` policy refusal.
 */
commands::CommandResponse execute_resource_command(
    const ::ServerConfig& config,
    std::string_view command,
    std::string args_json,
    std::string request_id,
    const commands::CallerContext& caller);

}  // namespace http
}  // namespace url_shortener
