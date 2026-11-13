#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/arg_spec.hpp>
#include <url_shortener/commands/builtin_commands.hpp>
#include <url_shortener/commands/catalog.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>

namespace url_shortener::commands
{

namespace
{

/**
 * @brief Escapes a descriptor's documentation text for a Markdown table cell.
 *
 * Replaces every `|` with `\|` so it cannot be read as a column separator, and
 * every backtick with its HTML entity so a stray backtick cannot open inline
 * code and swallow the rest of the row. The text is otherwise copied verbatim;
 * descriptor strings are one line, so no newline handling is needed.
 *
 * @param text The raw documentation/summary text to escape.
 * @return The escaped text, safe to place inside a single table cell.
 */
std::string markdown_escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        if (c == '|')
        {
            out += "\\|";
        }
        else if (c == '`')
        {
            out += "&#96;";
        }
        else
        {
            out += c;
        }
    }
    return out;
}

/**
 * @brief Wraps a token in backticks for Markdown inline code.
 *
 * @param text The token to wrap.
 * @return @p text surrounded by backticks.
 */
std::string code_span(std::string_view text)
{
    std::string out = "`";
    out += text;
    out += "`";
    return out;
}

/**
 * @brief Renders the `Type` column cell for one argument.
 *
 * Uses the @ref arg_type_name vocabulary; for an @ref ArgType::enumeration it
 * appends the allowed values as backtick-wrapped tokens joined by the escaped
 * table separator `\|`.
 *
 * @param arg The argument spec.
 * @return The cell text for the catalog `Type` column.
 */
std::string catalog_type_cell(const ArgSpec& arg)
{
    std::string out(arg_type_name(arg.type));
    if (arg.type == ArgType::enumeration && !arg.enum_values.empty())
    {
        out += " ";
        bool first = true;
        for (const std::string& value : arg.enum_values)
        {
            if (!first)
            {
                out += "\\|";
            }
            first = false;
            out += code_span(value);
        }
    }
    return out;
}

/**
 * @brief Renders the `Req` column cell for one argument.
 *
 * @param arg The argument spec.
 * @return `one of` when the argument belongs to a mutually-exclusive group,
 * `yes` when it is required, otherwise `no`.
 */
std::string_view req_cell(const ArgSpec& arg)
{
    if (!arg.one_of_group.empty())
    {
        return "one of";
    }
    return arg.required ? "yes" : "no";
}

/**
 * @brief Builds the metadata suffix of a catalog detail-block heading.
 *
 * Produces `` Perm `write`, sync, Mut, `dry_run` yes `` where the `Mut` and
 * `` `dry_run` yes `` segments are present only when the descriptor is
 * mutating / supports dry-run respectively.
 *
 * @param descriptor The descriptor whose metadata is summarized.
 * @return The comma-joined heading suffix.
 */
std::string heading_meta(const CommandDescriptor& descriptor)
{
    std::string out = "Perm ";
    out += code_span(permission_name(descriptor.permission));
    out += ", ";
    out += kind_name(descriptor.kind);
    if (descriptor.mutating)
    {
        out += ", Mut";
    }
    if (descriptor.supports_dry_run)
    {
        out += ", ";
        out += code_span("dry_run");
        out += " yes";
    }
    return out;
}

/**
 * @brief Lists every argument name sharing one @ref ArgSpec::one_of_group, in
 * declaration order.
 *
 * @param descriptor The descriptor to scan.
 * @param group The non-empty group tag to collect.
 * @return The member names joined by `, `.
 */
std::string one_of_members(const CommandDescriptor& descriptor,
                           std::string_view group)
{
    std::string out;
    for (const ArgSpec& arg : descriptor.args)
    {
        if (arg.one_of_group == group)
        {
            if (!out.empty())
            {
                out += ", ";
            }
            out += arg.name;
        }
    }
    return out;
}

/**
 * @brief Renders one argument line of @ref render_command_help.
 *
 * Assembles `--arg <name> <type> <required-word> [enum ...] [default ...]
 * <doc>` from non-empty tokens joined by a single space (so no segment ever
 * leaves a trailing space), prefixed by the two-space indent.
 *
 * @param descriptor The owning descriptor (needed to list `one_of` members).
 * @param arg The argument spec to render.
 * @return The complete help line, without a trailing newline.
 */
std::string help_arg_line(const CommandDescriptor& descriptor,
                         const ArgSpec& arg)
{
    std::vector<std::string> tokens;
    tokens.push_back("--arg");
    tokens.push_back(arg.name);
    tokens.push_back(std::string(arg_type_name(arg.type)));

    if (!arg.one_of_group.empty())
    {
        tokens.push_back("required (one of: " +
                         one_of_members(descriptor, arg.one_of_group) + ")");
    }
    else
    {
        tokens.push_back(arg.required ? "required" : "optional");
    }

    if (arg.type == ArgType::enumeration && !arg.enum_values.empty())
    {
        std::string values;
        for (const std::string& value : arg.enum_values)
        {
            if (!values.empty())
            {
                values += "|";
            }
            values += value;
        }
        tokens.push_back("[enum " + values + "]");
    }

    if (arg.default_value.has_value())
    {
        tokens.push_back("[default " + *arg.default_value + "]");
    }

    if (!arg.doc.empty())
    {
        tokens.push_back(arg.doc);
    }

    std::string line = "  ";
    bool first = true;
    for (const std::string& token : tokens)
    {
        if (!first)
        {
            line += " ";
        }
        first = false;
        line += token;
    }
    return line;
}

}  // namespace

std::string render_catalog_markdown(const CommandRegistry& registry)
{
    const std::vector<const CommandDescriptor*> descriptors = registry.list();

    std::vector<std::string> lines;
    lines.emplace_back("| Command | Perm | Kind | Mut | dry_run | Summary |");
    lines.emplace_back("|---|---|---|---|---|---|");

    for (const CommandDescriptor* descriptor : descriptors)
    {
        std::string row = "| ";
        row += code_span(descriptor->name);
        row += " | ";
        row += code_span(permission_name(descriptor->permission));
        row += " | ";
        row += kind_name(descriptor->kind);
        row += " | ";
        row += descriptor->mutating ? "yes" : "no";
        row += " | ";
        row += descriptor->supports_dry_run ? "yes" : "no";
        row += " | ";
        row += markdown_escape(descriptor->summary);
        row += " |";
        lines.push_back(std::move(row));
    }

    for (const CommandDescriptor* descriptor : descriptors)
    {
        lines.emplace_back("");

        std::string heading = "#### ";
        heading += code_span(descriptor->name);
        heading += " - ";
        heading += heading_meta(*descriptor);
        lines.push_back(std::move(heading));

        lines.push_back(markdown_escape(descriptor->summary));

        if (!descriptor->args.empty())
        {
            lines.emplace_back("");
            lines.emplace_back("| Arg | Type | Req | Description |");
            lines.emplace_back("|---|---|---|---|");
            for (const ArgSpec& arg : descriptor->args)
            {
                std::string row = "| ";
                row += code_span(arg.name);
                row += " | ";
                row += catalog_type_cell(arg);
                row += " | ";
                row += req_cell(arg);
                row += " | ";
                row += markdown_escape(arg.doc);
                row += " |";
                lines.push_back(std::move(row));
            }
        }

        if (!descriptor->maps_to.empty())
        {
            lines.emplace_back("");
            std::string maps = "Maps to: ";
            bool first = true;
            for (const std::string& target : descriptor->maps_to)
            {
                if (!first)
                {
                    maps += ", ";
                }
                first = false;
                maps += code_span(target);
            }
            maps += ".";
            lines.push_back(std::move(maps));
        }
    }

    std::string out;
    for (const std::string& line : lines)
    {
        out += line;
        out += "\n";
    }
    return out;
}

std::string render_command_help(const CommandDescriptor& descriptor)
{
    std::vector<std::string> lines;

    lines.push_back("Usage: url_shortener cmd " + descriptor.name +
                   " [--json '<args-object>' | --arg key=value ...]");
    lines.push_back(descriptor.summary);

    if (descriptor.args.empty())
    {
        lines.emplace_back("Arguments: (none)");
    }
    else
    {
        lines.emplace_back("Arguments:");
        for (const ArgSpec& arg : descriptor.args)
        {
            lines.push_back(help_arg_line(descriptor, arg));
        }
    }

    std::string footer = "Permission: ";
    footer += permission_name(descriptor.permission);
    footer += "; mutating: ";
    footer += descriptor.mutating ? "yes" : "no";
    footer += "; dry_run: ";
    footer += descriptor.supports_dry_run ? "yes" : "no";
    footer += "; kind: ";
    footer += kind_name(descriptor.kind);
    lines.push_back(std::move(footer));

    std::string out;
    for (const std::string& line : lines)
    {
        out += line;
        out += "\n";
    }
    return out;
}

}  // namespace url_shortener::commands
