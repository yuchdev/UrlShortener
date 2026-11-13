#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/builtin_commands.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Serializes one @ref ArgSpec to its public catalog JSON object.
 *
 * Emits `{"name","type","required"[,"enum":[...]][,"default":<token>],"doc"}`
 * in that fixed key order. `"enum"` appears only for an @ref
 * ArgType::enumeration argument (its values escaped through `jsonString`);
 * `"default"` appears only when @ref ArgSpec::default_value is set and is
 * embedded verbatim as the descriptor's trusted raw JSON token. No other
 * @ref ArgSpec field (bounds, nullability, nesting, grouping) is part of the
 * catalog shape, so none is emitted (threat model T-20).
 *
 * @param arg The argument spec to serialize.
 * @return One well-formed JSON object as text.
 */
std::string arg_to_json(const ArgSpec& arg)
{
    std::string out = "{\"name\":";
    out += jsonString(arg.name);
    out += ",\"type\":";
    out += jsonString(std::string(arg_type_name(arg.type)));
    out += ",\"required\":";
    out += arg.required ? "true" : "false";
    if (arg.type == ArgType::enumeration) {
        out += ",\"enum\":[";
        bool first = true;
        for (const std::string& value : arg.enum_values) {
            if (!first) {
                out += ",";
            }
            first = false;
            out += jsonString(value);
        }
        out += "]";
    }
    if (arg.default_value.has_value()) {
        out += ",\"default\":";
        out += *arg.default_value;
    }
    out += ",\"doc\":";
    out += jsonString(arg.doc);
    out += "}";
    return out;
}

}  // namespace

std::string_view permission_name(Permission p)
{
    switch (p) {
        case Permission::read:
            return "read";
        case Permission::write:
            return "write";
        case Permission::migrate:
            return "migrate";
        case Permission::manage_users:
            return "manage_users";
    }
    return "read";  // Unreachable for a valid enumerator.
}

std::string_view kind_name(ExecutionKind k)
{
    switch (k) {
        case ExecutionKind::sync:
            return "sync";
        case ExecutionKind::job:
            return "job";
    }
    return "sync";  // Unreachable for a valid enumerator.
}

std::string_view arg_type_name(ArgType t)
{
    switch (t) {
        case ArgType::string:
            return "string";
        case ArgType::integer:
            return "int";
        case ArgType::boolean:
            return "bool";
        case ArgType::enumeration:
            return "enum";
        case ArgType::timestamp:
            return "timestamp";
        case ArgType::string_list:
            return "list<string>";
        case ArgType::string_map:
            return "map<string,string>";
        case ArgType::object:
            return "object";
    }
    return "string";  // Unreachable for a valid enumerator.
}

std::string descriptor_to_json(const CommandDescriptor& descriptor)
{
    std::string out = "{\"name\":";
    out += jsonString(descriptor.name);
    out += ",\"summary\":";
    out += jsonString(descriptor.summary);
    out += ",\"perm\":";
    out += jsonString(std::string(permission_name(descriptor.permission)));
    out += ",\"kind\":";
    out += jsonString(std::string(kind_name(descriptor.kind)));
    out += ",\"mutating\":";
    out += descriptor.mutating ? "true" : "false";
    out += ",\"dry_run\":";
    out += descriptor.supports_dry_run ? "true" : "false";
    out += ",\"args\":[";
    bool first = true;
    for (const ArgSpec& arg : descriptor.args) {
        if (!first) {
            out += ",";
        }
        first = false;
        out += arg_to_json(arg);
    }
    out += "]}";
    return out;
}

void register_builtin_commands(CommandRegistry& registry)
{
    CommandDescriptor descriptor;
    descriptor.name = "command.list";
    descriptor.summary = "List all commands and their argument schemas";
    descriptor.permission = Permission::read;
    descriptor.kind = ExecutionKind::sync;
    descriptor.mutating = false;
    descriptor.handler = [&registry](CommandContext&,
                                     const ArgsView&) -> CommandResult
    {
        std::string result = "{\"commands\":[";
        bool first = true;
        for (const CommandDescriptor* entry : registry.list()) {
            if (!first) {
                result += ",";
            }
            first = false;
            result += descriptor_to_json(*entry);
        }
        result += "]}";

        CommandResult out;
        out.result_json = std::move(result);
        return out;
    };

    registry.add(std::move(descriptor));
}

void register_documented_commands(CommandRegistry& registry)
{
    register_builtin_commands(registry);
}

}  // namespace url_shortener::commands
