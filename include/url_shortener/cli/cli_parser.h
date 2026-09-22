/**
 * @file cli_parser.h
 * @brief CLI argument parsing contract for url_shortener.
 */
#pragma once

#include <optional>
#include <string>
#include <variant>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/config.h>

/// Link-management subcommand recognized as `link <verb>` on the command line.
///
/// The CLI exposes one verb per in-scope management operation; each maps to a
/// single `app::LinkCommandService` method. `del` stands for the `delete`
/// token (which is a C++ keyword) to keep the enumerator spellable.
enum class LinkCliVerb
{
    create,   ///< `link create`  -> LinkCommandService::CreateLink.
    get,      ///< `link get`     -> LinkCommandService::GetLink.
    update,   ///< `link update`  -> LinkCommandService::UpdateLink.
    del,      ///< `link delete`  -> LinkCommandService::DeleteLink.
    enable,   ///< `link enable`  -> LinkCommandService::SetLinkEnabled(true).
    disable,  ///< `link disable` -> LinkCommandService::SetLinkEnabled(false).
    restore,  ///< `link restore` -> LinkCommandService::RestoreLink.
    preview,  ///< `link preview` -> LinkCommandService::PreviewLink.
    stats     ///< `link stats`   -> LinkCommandService::GetLinkStats.
};

/// A recognized `link <verb>` invocation and its parsed argument DTO.
///
/// @ref payload holds exactly one of the shared `app::` command/query DTOs,
/// selected by @ref verb. Reusing those DTOs (rather than CLI-local structs)
/// keeps a single command contract shared by the CLI and REST adapters. The
/// per-verb flag-to-DTO mapping is populated by later subtasks; this variant
/// only carries the discriminated holder.
struct LinkCliCommand
{
    LinkCliVerb verb = LinkCliVerb::get;  ///< Which link verb was recognized.
    std::variant<
        url_shortener::app::CreateLinkCommand,
        url_shortener::app::GetLinkQuery,
        url_shortener::app::UpdateLinkCommand,
        url_shortener::app::DeleteLinkCommand,
        url_shortener::app::SetLinkEnabledCommand,
        url_shortener::app::RestoreLinkCommand,
        url_shortener::app::GetLinkStatsQuery>
        payload;  ///< Parsed DTO for @ref verb (default-constructed for now).
};

/// Result returned by CliParser::parse().
///
/// When help_requested is true, help_text contains the formatted usage
/// message and the caller should print it and exit successfully.
struct ParseResult
{
    ServerConfig config;  ///< Parsed server configuration.
    bool help_requested = false;  ///< Indicates `--help` short-circuit.
    std::string help_text;  ///< Rendered help output.
    /// Set only for `link <verb>` invocations; std::nullopt for server mode.
    std::optional<LinkCliCommand> command;
};

/// Parses command-line arguments using Boost.ProgramOptions and converts
/// them into a ServerConfig.
///
/// Responsibilities:
///  - Recognizes a leading `link <verb>` positional as a one-shot
///    link-management command and returns it via ParseResult::command,
///    short-circuiting all server-flag parsing.
///  - Defines and documents every supported CLI option in one place.
///  - Reads the SHORTENER_BASE_DOMAIN environment variable as a default.
///  - Validates argument values and produces clear error messages.
///  - Handles --help by populating ParseResult::help_text and setting
///    ParseResult::help_requested.
class CliParser
{
public:
    /// Parse argc/argv and return a ParseResult.
    ///
    /// Throws std::invalid_argument on unknown or invalid arguments.
    /// When --help is present, help_requested is set and config may be
    /// partially populated – callers should check help_requested first.
    ParseResult parse(int argc, char* argv[]) const;
};
