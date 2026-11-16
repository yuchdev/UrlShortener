#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/ip/address.hpp>

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
 * @brief Returns the one command runtime a resource route executes against,
 * shared across every request rather than rebuilt per request.
 *
 * Prefers the process-wide runtime `main` installs once at startup via
 * `composition::install_process_command_runtime` (built from the **active**
 * `app::LinkBackendHandle`, so `link.list` / `db.rows links` read the same
 * store link CRUD writes to — no split brain under `--storage-config`). When no
 * process runtime is installed (unit tests, tools that never run `main`'s
 * startup wiring), it returns a single fallback runtime built lazily on first
 * use over the legacy process-wide holders (an inactive handle: the
 * `LegacyLinkLister` over the global `linkRepository()` and the link service
 * over `app::LinkBackend::store()`), with a fixed default `ServerConfig`. Both
 * branches return an owning `std::shared_ptr`, so the runtime stays alive for
 * the caller's use even if teardown resets the holder concurrently; and both
 * are built at most once, never per request (contract C10 keeps this off the
 * redirect hot path; this is the management plane).
 *
 * @return An owning `std::shared_ptr` to the shared runtime; never null.
 * @throws std::runtime_error only on a startup-class wiring failure while
 * building the fallback runtime (propagated from
 * @ref composition::build_command_runtime); expected conditions never throw.
 */
std::shared_ptr<composition::CommandRuntime> request_runtime();

/**
 * @brief Returns the process-wide create-throttle rate limiter (threat model
 * T-75) the REST create adapter keys on the client peer address, or null when
 * no limiter is wired.
 *
 * Reads the `create_rate_limiter` carried on the shared runtime
 * (@ref request_runtime): in production the limiter `main` selected at startup
 * (the dedicated in-memory default, or the configured Redis limiter), shared by
 * every create request without a new process singleton. A null result (the
 * bridge's unit-test fallback runtime, which is built from a handle with no
 * limiter) is the signal to the adapter that the throttle is not wired and must
 * be skipped — fail-open, behavior-neutral for callers that never ran `main`'s
 * startup wiring.
 *
 * @return An owning `std::shared_ptr` to the selected create limiter, or null
 * when none is wired on the shared runtime.
 */
std::shared_ptr<::IRateLimiter> create_rate_limiter();

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
 * Obtains the shared runtime (@ref request_runtime) and delegates to the
 * explicit-registry overload with @ref resource_route_caller. Inherits the
 * fail-closed-while-serving behavior of the explicit-caller overload below.
 *
 * @param config Server configuration; retained for signature symmetry with the
 * REST handlers (subtasks 07/09). The runtime is process-wide and config-bound
 * at startup, so this argument is no longer read here; in production it equals
 * the config the installed runtime was built from.
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
 * (subtask 08). Obtains the shared runtime (@ref request_runtime) and delegates
 * to the explicit-registry overload.
 *
 * **Fail-closed while serving (security finding 04.0/03):** when the process
 * has latched `app::LinkBackend::serving()` yet no process runtime was
 * installed via `composition::install_process_command_runtime`, this overload
 * does **not** fall back to @ref request_runtime's lazily-built default (whose
 * link service borrows `app::LinkBackend::store()` by reference and is only
 * valid for the non-serving unit-test/tool case). It instead returns a
 * value-free `internal` failure, which @ref resource_response renders as `500`
 * `internal_error`, rather than risk executing against a dangling borrow. The
 * fallback runtime is still used normally when the process is not serving.
 *
 * @param config Server configuration; retained for signature symmetry with the
 * REST handlers (subtasks 07/09). The runtime is process-wide and config-bound
 * at startup, so this argument is no longer read here; in production it equals
 * the config the installed runtime was built from.
 * @param command The command name to run.
 * @param args_json The exact `args` object JSON text; moved into the request.
 * @param request_id Correlation id echoed in the response; moved into the
 * request.
 * @param caller The authenticated caller; `control_set` is the sole
 * authorization input.
 * @return The execution response; a `manage_users` policy refusal; or a
 * value-free `internal` failure when the process is serving with no process
 * runtime installed (the fail-closed case above).
 */
commands::CommandResponse execute_resource_command(
    const ::ServerConfig& config,
    std::string_view command,
    std::string args_json,
    std::string request_id,
    const commands::CallerContext& caller);

/**
 * @brief Whether the opt-in command API surface is enabled (milestone 0006
 * Task 04.0 subtask 08).
 *
 * The one gate check every gated route performs first. When it returns `false`,
 * the route answers exactly like an unmatched route, so no caller reaches the
 * underlying command (threat model T-64). A thin accessor over
 * `config.command_api_enabled`, kept in the bridge so the config header does not
 * depend on the security/control types.
 *
 * @param config Server configuration whose `command_api_enabled` flag is read.
 * @return `true` when the command API surface is enabled; `false` (the secure
 * default) otherwise.
 */
bool command_api_enabled(const ::ServerConfig& config);

/**
 * @brief Maps the configured command-API role to a `ControlSet`, before the
 * peer-locality check.
 *
 * Pure mapping of the role string to a role: `"admin"` yields
 * `ControlSet::Admin`, every other value (including the default `"user"`)
 * yields `ControlSet::User`. This is the role the caller *would* run as if the
 * peer check also passed; @ref command_api_caller combines it with the peer
 * locality to produce the effective caller.
 *
 * @param config Server configuration whose `command_api_role` string is read.
 * @return `ControlSet::Admin` when `config.command_api_role == "admin"`;
 * `ControlSet::User` for any other value.
 */
ControlSet command_api_control_set(const ::ServerConfig& config);

/**
 * @brief Whether a peer address is loopback and therefore locally trusted.
 *
 * `true` for the IPv4 loopback block `127.0.0.0/8`, the IPv6 loopback `::1`, and
 * IPv4-mapped IPv6 loopback (`::ffff:127.0.0.0/8`). Every acceptor currently
 * binds `tcp::v4()` (verified in `http_server.cpp`), so peers are always IPv4
 * today; the IPv6 branches are kept (cheap, unit-tested) so a future dual-stack
 * listener does not silently grant or lose the loopback-only admin escalation.
 * The listeners bind all interfaces, so this peer check — not a bind-address
 * restriction — is the sole locality guard (threat model T-64).
 *
 * @param peer The connection's resolved TCP peer address.
 * @return `true` when @p peer is an IPv4/IPv6/IPv4-mapped loopback address;
 * `false` for every routable address.
 */
bool is_loopback_peer(const boost::asio::ip::address& peer);

/**
 * @brief Builds the caller every gated route executes as (the only auth
 * mechanism in milestone 0006, subtask 08).
 *
 * Fixed, non-identifying `principal` (`"command-api"`) and `transport`
 * (`"rest"`) labels that never carry the peer IP, a token, or any request value
 * (threat model T-84). The effective `control_set` is `Admin` **only** when the
 * configured role is `"admin"` **and** @p peer is present and loopback
 * (@ref is_loopback_peer); every other case — role `"user"`, a non-loopback
 * peer, or an absent peer (`std::nullopt`, treated as non-loopback) — yields
 * `User`. This fail-closed combination is the stand-in for authentication until
 * Milestone 0002 (threat model T-64/T-70).
 *
 * @param config Server configuration supplying the configured role.
 * @param peer The connection's TCP peer address, or `std::nullopt` when no live
 * socket is available (unit tests, tools) or `remote_endpoint()` failed; an
 * absent peer is treated as non-loopback (`User`).
 * @return A @ref commands::CallerContext with the fixed labels and the
 * fail-closed effective role.
 */
commands::CallerContext command_api_caller(
    const ::ServerConfig& config,
    const std::optional<boost::asio::ip::address>& peer);

}  // namespace http
}  // namespace url_shortener
