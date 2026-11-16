#include <memory>
#include <string>
#include <utility>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/link_command_codec.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/command_bridge.hpp>
#include <url_shortener/http/request_handlers.hpp>

namespace url_shortener
{
namespace http
{

commands::CallerContext resource_route_caller()
{
    commands::CallerContext caller;
    // Fixed, non-identifying labels: never a peer IP, token, or request value
    // (threat model T-84). The Admin role preserves today's open resource
    // routes exactly (threat model T-60); Milestone 0002 replaces it.
    caller.principal = "rest-resource";
    caller.control_set = ControlSet::Admin;
    caller.transport = "rest";
    return caller;
}

namespace
{

/**
 * @brief The fallback command runtime used when no process-wide runtime was
 * installed (unit tests and tools that never call `main`'s startup wiring).
 *
 * Built exactly once, lazily, over the legacy process-wide holders (an inactive
 * @ref app::LinkBackendHandle, so `build_command_runtime` selects the
 * `LegacyLinkLister` over the global `linkRepository()` and binds the link
 * service to `app::LinkBackend::store()`), using a fixed default `ServerConfig`
 * that lives for the whole process. The function-local statics make the one-time
 * build thread-safe (C++11 static init) and never per request; the
 * `ServerConfig` static is declared before the runtime static so it is destroyed
 * after it, satisfying the service's "config outlives the runtime" contract.
 * @return The single shared fallback runtime; never null.
 */
std::shared_ptr<composition::CommandRuntime> default_request_runtime()
{
    static const ::ServerConfig s_default_config;
    static const std::shared_ptr<composition::CommandRuntime> s_runtime =
        std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(s_default_config,
                                               app::LinkBackendHandle {}));
    return s_runtime;
}

}  // namespace

std::shared_ptr<composition::CommandRuntime> request_runtime()
{
    // Prefer the one process-wide runtime main built from the active backend
    // handle (so link.list / db.rows read the same store link CRUD writes to —
    // no split brain under --storage-config). When none is installed, fall back
    // to the lazily-built-once legacy default, never a per-request build.
    if (std::shared_ptr<composition::CommandRuntime> installed =
            composition::process_command_runtime())
    {
        return installed;
    }
    return default_request_runtime();
}

std::optional<commands::CommandResponse> rest_policy_refusal(
    const commands::CommandRegistry& registry,
    std::string_view command,
    std::string_view id)
{
    const commands::CommandDescriptor* descriptor = registry.find(command);
    if (descriptor != nullptr
        && descriptor->permission == commands::Permission::manage_users)
    {
        // Transport rule: this transport never carries user management. Refuse
        // for every role/peer, including Admin, before execute (T-61). The
        // fixed message matches execute's own denial message so REST bodies are
        // identical whichever path denies (contract C3), and carries no value.
        return commands::make_failure(
            std::string(id),
            commands::make_error(commands::CommandErrorCode::not_authorized,
                                 "permission denied"));
    }
    return std::nullopt;
}

std::string legacy_error_code(const commands::CommandError& error)
{
    // Promote exactly the two stats details the legacy codeForAppError
    // promotes, and only for invalid_field. Every other invalid_field message
    // (including the amended range_too_large and "slug: missing") keeps the
    // generic invalid_request code (threat model T-62).
    if (error.code == commands::CommandErrorCode::invalid_field
        && (error.message == commands::detail_invalid_timestamp
            || error.message == commands::detail_invalid_bucket))
    {
        return error.message;
    }
    return std::string(commands::error_info_for(error.code).legacy_rest_code);
}

BeastResponse resource_response(const BeastRequest& req,
                                const ::ServerConfig& config,
                                const bool is_tls,
                                const commands::CommandResponse& response,
                                const unsigned success_status)
{
    // A job-carrying response is never produced by a resource route; a resource
    // route has no job-rendering path, so surface it as internal (T-63).
    if (response.ok && response.job.has_value()) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 500, "internal_error", "internal error");
    }
    if (response.ok) {
        return url_shortener::makeResponse(req,
                                           config,
                                           is_tls,
                                           success_status,
                                           response.result_json,
                                           "application/json");
    }
    // Status and code come from the 01.0/02 legacy columns (which copy
    // statusForAppError/codeForAppError), and the message is the command/service
    // layer's fixed, value-free text, so the envelope is byte-identical to
    // today's (contract C3/C6).
    const commands::error_info& info =
        commands::error_info_for(response.error.code);
    return url_shortener::makeApiErrorResponse(req,
                                               config,
                                               is_tls,
                                               info.legacy_rest_status,
                                               legacy_error_code(response.error),
                                               response.error.message);
}

commands::CommandResponse execute_resource_command(
    const commands::CommandRegistry& registry,
    std::string_view command,
    std::string args_json,
    std::string request_id,
    const commands::CallerContext& caller)
{
    if (std::optional<commands::CommandResponse> refusal =
            rest_policy_refusal(registry, command, request_id))
    {
        return std::move(*refusal);
    }

    commands::CommandRequest request;
    request.v = 1;
    request.id = std::move(request_id);
    request.command = std::string(command);
    request.args_json = std::move(args_json);
    // options stays default ({}): resource routes never request dry_run/async.
    return registry.execute(caller, request);
}

commands::CommandResponse execute_resource_command(const ::ServerConfig& config,
                                                   std::string_view command,
                                                   std::string args_json,
                                                   std::string request_id)
{
    return execute_resource_command(config,
                                    command,
                                    std::move(args_json),
                                    std::move(request_id),
                                    resource_route_caller());
}

commands::CommandResponse execute_resource_command(
    const ::ServerConfig& config,
    std::string_view command,
    std::string args_json,
    std::string request_id,
    const commands::CallerContext& caller)
{
    // The runtime is process-wide and config-bound at startup (or the legacy
    // default's fixed config), so @p config is no longer read here; it stays in
    // the signature because the REST handlers (subtasks 07/09) hold the request
    // ServerConfig and the response renderers still take it. In production the
    // installed runtime was built from this same config.
    (void)config;
    const std::shared_ptr<composition::CommandRuntime> runtime =
        request_runtime();
    return execute_resource_command(*runtime->registry,
                                    command,
                                    std::move(args_json),
                                    std::move(request_id),
                                    caller);
}

}  // namespace http
}  // namespace url_shortener
