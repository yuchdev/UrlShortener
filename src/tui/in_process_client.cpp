#include <exception>
#include <utility>

#include "url_shortener/tui/in_process_client.hpp"

#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/security/control_set.hpp>

namespace url_shortener::tui
{

namespace
{

/**
 * @brief Maps a registry `ArgType` to its TUI @ref ArgKind.
 *
 * Total by construction: an unrecognized type (only producible by casting)
 * falls through to @ref ArgKind::string so the catalog can never crash on a
 * new registry type (subtask step 1).
 *
 * @param type The descriptor argument type.
 * @return The matching @ref ArgKind.
 */
ArgKind kind_for(commands::ArgType type)
{
    switch (type) {
        case commands::ArgType::string:
            return ArgKind::string;
        case commands::ArgType::integer:
            return ArgKind::integer;
        case commands::ArgType::boolean:
            return ArgKind::boolean;
        case commands::ArgType::enumeration:
            return ArgKind::enumeration;
        case commands::ArgType::timestamp:
            return ArgKind::timestamp;
        case commands::ArgType::string_list:
            return ArgKind::string_list;
        case commands::ArgType::string_map:
            return ArgKind::string_map;
        case commands::ArgType::object:
            return ArgKind::object;
    }
    return ArgKind::string;
}

/**
 * @brief Maps a base `Permission` to its wire string.
 *
 * @param permission The descriptor base permission.
 * @return `"read"`, `"write"`, `"migrate"` or `"manage_users"`.
 */
std::string permission_name(commands::Permission permission)
{
    switch (permission) {
        case commands::Permission::read:
            return "read";
        case commands::Permission::write:
            return "write";
        case commands::Permission::migrate:
            return "migrate";
        case commands::Permission::manage_users:
            return "manage_users";
    }
    return "read";
}

/**
 * @brief Converts one `ArgSpec` to an @ref ArgInfo view-model.
 *
 * @param spec The registry argument spec.
 * @return The TUI-facing @ref ArgInfo, with nested `fields` reduced to their
 * member names.
 */
ArgInfo arg_info_for(const commands::ArgSpec& spec)
{
    ArgInfo info;
    info.name = spec.name;
    info.kind = kind_for(spec.type);
    info.required = spec.required;
    info.nullable = spec.nullable;
    info.enum_values = spec.enum_values;
    info.default_value = spec.default_value;
    info.doc = spec.doc;
    info.members.reserve(spec.fields.size());
    for (const auto& field : spec.fields) {
        info.members.push_back(field.name);
    }
    return info;
}

}  // namespace

commands::CommandResponse reconcile_envelope(commands::CommandResponse response)
{
    const bool has_error =
        response.error.code != commands::CommandErrorCode::none;
    const bool has_result = !response.result_json.empty();
    const bool has_job = response.job.has_value();
    const bool has_success_payload = has_result || has_job;

    bool contradictory = false;
    if (response.ok) {
        // A success must carry exactly one success payload and no error.
        if (has_error || !has_success_payload || (has_result && has_job)) {
            contradictory = true;
        }
    }
    else {
        // A failure must carry an error and no success payload.
        if (!has_error || has_success_payload) {
            contradictory = true;
        }
    }

    if (!contradictory) {
        return response;
    }

    return commands::make_failure(
        std::move(response.id),
        commands::make_error(commands::CommandErrorCode::internal,
                             "internal error"));
}

InProcessClient::InProcessClient(const commands::CommandRegistry& registry,
                                 commands::CallerContext caller)
    : m_registry(registry)
    , m_caller(std::move(caller))
{
}

commands::CommandResponse InProcessClient::call(
    const commands::CommandRequest& request)
{
    commands::CommandResponse response;
    try {
        response = m_registry.execute(m_caller, request);
    } catch (...) {
        // execute() never throws, but a future handler bug must not kill the
        // UI: surface it as a value-free internal error (threat model T-10).
        return commands::make_failure(
            request.id,
            commands::make_error(commands::CommandErrorCode::internal,
                                 "internal error"));
    }
    return reconcile_envelope(std::move(response));
}

std::vector<CommandInfo> InProcessClient::commands() const
{
    std::vector<CommandInfo> out;
    const std::vector<const commands::CommandDescriptor*> descriptors =
        m_registry.list();
    out.reserve(descriptors.size());
    for (const commands::CommandDescriptor* descriptor : descriptors) {
        CommandInfo info;
        info.name = descriptor->name;
        info.summary = descriptor->summary;
        info.permission = permission_name(descriptor->permission);
        info.is_job = descriptor->kind == commands::ExecutionKind::job;
        info.mutating = descriptor->mutating;
        info.supports_dry_run = descriptor->supports_dry_run;
        info.args.reserve(descriptor->args.size());
        for (const commands::ArgSpec& spec : descriptor->args) {
            info.args.push_back(arg_info_for(spec));
        }
        out.push_back(std::move(info));
    }
    return out;
}

SessionInfo InProcessClient::session() const
{
    SessionInfo info;
    info.principal = m_caller.principal;
    info.read_only = !canWrite(m_caller.control_set);
    info.transport = m_caller.transport.empty() ? "tui" : m_caller.transport;
    return info;
}

}  // namespace url_shortener::tui
