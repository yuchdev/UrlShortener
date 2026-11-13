#pragma once

#include <string>
#include <string_view>

#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>

namespace url_shortener::commands
{

/**
 * @brief Opening HTML marker that delimits the generated command block inside
 * `docs/commands/README.md`.
 *
 * The dump tool (subtask 12) replaces everything between this marker and
 * @ref catalog_end_marker with the output of @ref render_catalog_markdown, the
 * way `tools/route_registry_dump` owns its block of `docs/api/README.md`
 * (decision log D9: only block `3.7` is generated; the surrounding prose is
 * hand-written and drift-tested). @ref render_catalog_markdown itself does
 * **not** emit the markers.
 */
inline constexpr std::string_view catalog_begin_marker =
    "<!-- BEGIN GENERATED: commands (tools/command_catalog_dump) -->";

/**
 * @brief Closing HTML marker that ends the generated command block inside
 * `docs/commands/README.md`; see @ref catalog_begin_marker.
 */
inline constexpr std::string_view catalog_end_marker =
    "<!-- END GENERATED: commands -->";

/**
 * @brief Renders the whole command catalog as deterministic GitHub-flavored
 * Markdown (milestone contract C1, C9).
 *
 * The output is an index table with the columns
 * `Command | Perm | Kind | Mut | dry_run | Summary`, followed by one detail
 * block per command in @ref CommandRegistry::list name order. Each block is a
 * `#### ` heading of the form
 * `` #### `link.create` - Perm `write`, sync, Mut, `dry_run` yes `` (the `Mut`
 * and `` `dry_run` yes `` segments appear only when the descriptor is mutating
 * / supports dry-run), the one-line summary, an argument table
 * `Arg | Type | Req | Description` when the command has arguments, and a final
 * `Maps to: ...` line when @ref CommandDescriptor::maps_to is non-empty. The
 * `Type` column uses the @ref arg_type_name vocabulary (subtask 10) so the
 * catalog, `command.list` and @ref render_command_help never disagree; an
 * argument in a @ref ArgSpec::one_of_group renders its `Req` cell as
 * `one of`.
 *
 * Determinism: the function reads only descriptor **metadata** (never calls a
 * handler or `execute`, constructs no service, touches no clock, environment or
 * locale), emits `\n` line endings only, no timestamps, and no trailing
 * spaces, so repeated runs and the two CI operating systems produce
 * byte-identical text (threat model, subtask 11/12). Any `|` or backtick in a
 * descriptor's documentation text is escaped so it cannot break a table row;
 * no field outside the descriptor (handler address, factory, permission hook)
 * is ever emitted.
 *
 * @param registry The registry whose descriptors are rendered; only its
 * metadata is read and it is not modified.
 * @return The Markdown block **without** the surrounding
 * @ref catalog_begin_marker / @ref catalog_end_marker; for an empty registry,
 * only the (header-only) index table.
 */
std::string render_catalog_markdown(const CommandRegistry& registry);

/**
 * @brief Renders the plain-text help body for one command (milestone contract
 * C1, C9).
 *
 * The body is what CLI `cmd <name> --help` and the TUI help pane print: a
 * `Usage:` line naming the generic `cmd` invocation, the one-line summary, an
 * `Arguments:` section, and a footer line
 * `Permission: <perm>; mutating: yes|no; dry_run: yes|no; kind: sync|job`.
 * Each argument is rendered as
 * `  --arg <name> <type> (required|optional) [enum a|b] [default <token>]
 * <doc>`, where the type comes from @ref arg_type_name, the `[enum ...]` and
 * `[default ...]` segments appear only when applicable, and an argument in a
 * @ref ArgSpec::one_of_group reads `required (one of: slug, id)` listing every
 * member of its group in declaration order. A command with no arguments prints
 * `Arguments: (none)`.
 *
 * Determinism: the function reads only descriptor **metadata** (no handler,
 * clock, environment or locale), emits `\n` line endings only and no trailing
 * whitespace, so its output is byte-identical across runs and operating
 * systems. The `[default <token>]` segment is the descriptor's trusted raw
 * JSON token embedded verbatim, as in @ref descriptor_to_json.
 *
 * @param descriptor The command descriptor to document; only its metadata is
 * read.
 * @return The multi-line help body terminated by a single `\n`.
 */
std::string render_command_help(const CommandDescriptor& descriptor);

}  // namespace url_shortener::commands
