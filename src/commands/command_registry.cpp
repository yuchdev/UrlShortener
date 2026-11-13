#include "url_shortener/commands/command_registry.hpp"

#include <algorithm>
#include <exception>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "url_shortener/commands/arg_spec.hpp"
#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"
#include "url_shortener/security/access_guard.hpp"

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Checks whether a caller's role satisfies a required permission,
 * without leaking the outcome as an exception.
 *
 * Builds an @ref AccessGuard from `caller.control_set` and invokes the
 * `require*` method matching @p permission; the guard throws
 * @ref AccessDeniedError on denial, which this helper catches and reports as
 * `false`. This is the single place that maps a @ref Permission onto the
 * 1:1 `ControlSet` capability queries (`read`->`requireRead`, `write`->
 * `requireWrite`, `migrate`->`requireMigration`, `manage_users`->
 * `requireUserManagement`); no other command-layer code performs an
 * authorization check (milestone contract C7).
 *
 * @param caller The authenticated caller; only `control_set` is consulted.
 * @param permission The permission the command requires.
 * @return `true` when the caller's `control_set` holds @p permission; `false`
 * when the guard denies it. Never throws for a denial.
 */
bool permission_check(const CallerContext& caller, Permission permission)
{
    const AccessGuard guard(caller.control_set);
    try
    {
        switch (permission)
        {
        case Permission::read:
            guard.requireRead();
            break;
        case Permission::write:
            guard.requireWrite();
            break;
        case Permission::migrate:
            guard.requireMigration();
            break;
        case Permission::manage_users:
            guard.requireUserManagement();
            break;
        }
    }
    catch (const AccessDeniedError&)
    {
        return false;
    }
    return true;
}

}  // namespace

bool CommandRegistry::is_valid_descriptor(const CommandDescriptor& descriptor,
                                          std::string* why)
{
    // Writes a fixed reason into `why` (when provided) and returns false; the
    // reasons are fixed developer-facing text, never caller-supplied values.
    const auto fail = [&](const char* reason) {
        if (why != nullptr)
        {
            *why = reason;
        }
        return false;
    };

    if (!is_valid_command_name(descriptor.name))
    {
        return fail("invalid command name");
    }
    if (descriptor.summary.empty())
    {
        return fail("empty summary");
    }
    if (descriptor.kind == ExecutionKind::sync && !descriptor.handler)
    {
        return fail("null handler for sync command");
    }
    if ((descriptor.kind == ExecutionKind::job || descriptor.async_capable) &&
        !descriptor.job_factory)
    {
        return fail("missing job_factory");
    }

    std::set<std::string_view> seen_arg_names;
    for (const ArgSpec& arg : descriptor.args)
    {
        if (!seen_arg_names.insert(arg.name).second)
        {
            return fail("duplicate arg name");
        }
    }

    if (descriptor.supports_dry_run && !descriptor.mutating)
    {
        return fail("supports_dry_run requires mutating");
    }

    // A state-changing command must not be runnable with read permission
    // alone; the descriptor is the only authorization source (C7). A
    // permission_for hook may still raise a read base per call (D16).
    if (descriptor.mutating && descriptor.permission == Permission::read
        && !descriptor.permission_for)
    {
        return fail("mutating requires write permission");
    }

    // For job commands the sync handler is the dry_run path; without it a
    // dry_run request would have nothing to dispatch to.
    if (descriptor.supports_dry_run && descriptor.kind == ExecutionKind::job
        && !descriptor.handler)
    {
        return fail("dry_run job requires handler");
    }

    if (why != nullptr)
    {
        why->clear();
    }
    return true;
}

void CommandRegistry::add(CommandDescriptor descriptor)
{
    std::string why;
    if (!is_valid_descriptor(descriptor, &why))
    {
        throw std::invalid_argument(why);
    }

    // Copy the key out before moving the descriptor: argument evaluation order
    // within emplace is unspecified, so reading descriptor.name after a move
    // from descriptor would be unsafe.
    std::string key = descriptor.name;
    const bool inserted =
        m_commands.emplace(std::move(key), std::move(descriptor)).second;
    if (!inserted)
    {
        throw std::invalid_argument("duplicate command name");
    }
}

const CommandDescriptor* CommandRegistry::find(std::string_view name) const
{
    const auto it = m_commands.find(name);
    return it == m_commands.end() ? nullptr : &it->second;
}

std::vector<const CommandDescriptor*> CommandRegistry::list() const
{
    std::vector<const CommandDescriptor*> result;
    result.reserve(m_commands.size());
    for (const auto& entry : m_commands)
    {
        result.push_back(&entry.second);
    }
    return result;
}

CommandResponse CommandRegistry::execute(const CallerContext& caller,
                                         const CommandRequest& request) const
{
    // Step 1: envelope sanity (defense in depth; parse_request normally
    // catches these). No input value is echoed.
    if (request.v != 1 || request.command.empty())
    {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::invalid_envelope, "invalid envelope"));
    }

    // Step 2: lookup. The unknown name is deliberately not echoed.
    const CommandDescriptor* descriptor = find(request.command);
    if (descriptor == nullptr)
    {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::unknown_command, "unknown command"));
    }

    // Step 3: base permission, BEFORE argument validation, so an unauthorized
    // caller learns nothing about the arg schema and the handler is never
    // entered on denial (threat model T-08).
    if (!permission_check(caller, descriptor->permission))
    {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::not_authorized, "permission denied"));
    }

    // Step 4: argument validation. The message names the arg and a fixed
    // reason, never an argument value (contract C6).
    ArgsView view;
    CommandError arg_error;
    if (!validate_args(descriptor->args, request.args_json, &view, &arg_error))
    {
        return make_failure(request.id, std::move(arg_error));
    }

    // Step 4a: argument-dependent permission re-check (decision log D16). The
    // hook runs only on the validated view; the effective permission is
    // max(base, hook) over the contractual order, so it can only tighten
    // (threat model T-09). A throwing hook maps to internal.
    if (descriptor->permission_for)
    {
        Permission required = descriptor->permission;
        try
        {
            const Permission hinted = descriptor->permission_for(view);
            required = std::max(descriptor->permission, hinted);
        }
        catch (const std::exception&)
        {
            return make_failure(
                request.id,
                make_error(CommandErrorCode::internal, "internal error"));
        }
        catch (...)
        {
            return make_failure(
                request.id,
                make_error(CommandErrorCode::internal, "internal error"));
        }

        if (!permission_check(caller, required))
        {
            return make_failure(
                request.id,
                make_error(CommandErrorCode::not_authorized,
                           "permission denied"));
        }
    }

    // Step 5: options/job handling. dry_run, async and job dispatch are
    // subtask 08; until then they fail closed with `unsupported`.
    if (request.options.dry_run || request.options.async ||
        descriptor->kind == ExecutionKind::job)
    {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::unsupported,
                       "option not supported yet"));
    }

    // Step 6: dispatch with exception isolation. e.what() is never surfaced:
    // it may carry paths, SQL or a DSN (threat model T-10).
    CommandResult result;
    try
    {
        CommandContext ctx {caller, request, false};
        result = descriptor->handler(ctx, view);
    }
    catch (const std::exception&)
    {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::internal, "internal error"));
    }
    catch (...)
    {
        return make_failure(
            request.id,
            make_error(CommandErrorCode::internal, "internal error"));
    }

    // Step 7: convert the handler outcome into a response.
    if (!result.ok())
    {
        return make_failure(request.id, std::move(result.error));
    }
    if (result.result_json.empty())
    {
        // A successful handler must produce a payload; an empty one is a
        // handler programming error, reported as internal.
        return make_failure(
            request.id,
            make_error(CommandErrorCode::internal, "internal error"));
    }
    return make_success(request.id, std::move(result.result_json));
}

CommandResponse CommandRegistry::execute_json(
    const CallerContext& caller, std::string_view request_text,
    const envelope_limits& limits) const
{
    parse_request_result parsed = parse_request(request_text, limits);
    if (!parsed.ok())
    {
        // The parser already resolved a safe echo id (valid client id or a
        // srv-<n> fallback, threat model T-05) and a value-free envelope error.
        return make_failure(std::move(parsed.id), std::move(parsed.error));
    }
    return execute(caller, *parsed.request);
}

}  // namespace url_shortener::commands
