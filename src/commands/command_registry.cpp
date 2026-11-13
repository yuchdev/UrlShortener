#include "url_shortener/commands/command_registry.hpp"

#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "url_shortener/commands/envelope.hpp"
#include "url_shortener/commands/error_codes.hpp"

namespace url_shortener::commands
{

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
    // Subtask 06 stub: the real lookup/authorize/validate/dispatch pipeline is
    // implemented in subtask 07. Until then every call reports `internal` so
    // the registry can be built and exercised independently.
    (void)caller;
    return make_failure(
        request.id,
        make_error(CommandErrorCode::internal, "not implemented"));
}

}  // namespace url_shortener::commands
