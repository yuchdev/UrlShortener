/**
 * @file system_commands.cpp
 * @brief The `system.info` command descriptor and handler (subtask 17). The
 * handler serializes an immutable @ref url_shortener::app::SystemInfo snapshot
 * captured by value; it reads no live config and discloses only allowlisted
 * backend names and the non-secret `ServerConfig` subset (milestone contract
 * C6, threat model T-53).
 */
#include <url_shortener/commands/system_commands.hpp>

#include <utility>

#include <url_shortener/app/system_info.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/security/control_set.hpp>

namespace url_shortener::commands
{

CommandDescriptor make_system_info_descriptor(app::SystemInfo info)
{
    CommandDescriptor descriptor;
    descriptor.name = "system.info";
    descriptor.summary =
        "Report the server version, selected storage backends and non-secret "
        "shortener settings.";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.supports_dry_run = false;
    descriptor.maps_to = {"system info"};
    descriptor.handler =
        [info = std::move(info)](CommandContext& context, const ArgsView&)
    {
        CommandResult out;
        // read_only mirrors the caller's authority: a role that cannot write is
        // read-only (ADR 0002 Authorization); the snapshot has no auth data.
        const bool read_only = !canWrite(context.caller.control_set);
        out.result_json = app::serializeSystemInfoJson(info, read_only);
        return out;
    };
    return descriptor;
}

}  // namespace url_shortener::commands
