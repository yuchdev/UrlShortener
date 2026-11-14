/**
 * @file cli_parser.hpp
 * @brief CLI argument parsing contract for url_shortener.
 */
#pragma once

#include <optional>
#include <string>
#include <variant>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/config.hpp>

/// Link-management subcommand recognized as `link <verb>` on the command line.
///
/// The CLI exposes one verb per in-scope management operation; each maps to a
/// single `app::LinkCommandService` method. `del` stands for the `delete`
/// token (which is a C++ keyword) to keep the enumerator spellable.
enum class LinkCliVerb
{
    ///< `link create` -> LinkCommandService::CreateLink.
    create,

    ///< `link get` -> LinkCommandService::GetLink.
    get,

    ///< `link update` -> LinkCommandService::UpdateLink.
    update,

    ///< `link delete` -> LinkCommandService::DeleteLink.
    del,

    ///< `link enable` -> LinkCommandService::SetLinkEnabled(true).
    enable,

    ///< `link disable` -> LinkCommandService::SetLinkEnabled(false).
    disable,

    ///< `link restore` -> LinkCommandService::RestoreLink.
    restore,

    ///< `link preview` -> LinkCommandService::PreviewLink.
    preview,

    ///< `link stats` -> LinkCommandService::GetLinkStats.
    stats
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
    ///< Which link verb was recognized.
    LinkCliVerb verb = LinkCliVerb::get;

    ///< Parsed DTO for @ref verb (default-constructed for now).
    std::variant<url_shortener::app::CreateLinkCommand,
                 url_shortener::app::GetLinkQuery,
                 url_shortener::app::UpdateLinkCommand,
                 url_shortener::app::DeleteLinkCommand,
                 url_shortener::app::SetLinkEnabledCommand,
                 url_shortener::app::RestoreLinkCommand,
                 url_shortener::app::GetLinkStatsQuery>
        payload;
};

/// Result returned by CliParser::parse().
///
/// When help_requested is true, help_text contains the formatted usage
/// message and the caller should print it and exit successfully.
struct ParseResult
{
    ///< Parsed server configuration.
    ServerConfig config;

    ///< Indicates `--help` short-circuit.
    bool help_requested = false;

    ///< Rendered help output.
    std::string help_text;

    /// Set only for `link <verb>` invocations; std::nullopt for server mode.
    std::optional<LinkCliCommand> command;
};

/**
 * @brief Parses command-line arguments using Boost.ProgramOptions into
 * a `ParseResult` containing the server configuration, an optional
 * link-management command, and help metadata.
 *
 * Responsibilities:
 *  - Reads a fixed set of environment variables as defaults before
 *    applying CLI flags; see `parse()` for the full list.
 *  - Recognizes a leading `link <verb>` positional as a one-shot
 *    link-management command and returns it via `ParseResult::command`,
 *    short-circuiting all server-flag parsing.
 *  - Defines and documents every supported CLI option in one place.
 *  - Validates argument values and produces clear error messages.
 *  - Handles `--help` and `link [<verb>] --help` by populating
 *    `ParseResult::help_text` and setting `ParseResult::help_requested`.
 */
class CliParser
{
public:
    /**
     * @brief Parses @p argc / @p argv, applying environment-variable
     * defaults first, then CLI flag overrides.
     *
     * The following environment variables are read before any flag:
     * `SHORTENER_BASE_DOMAIN`, `SHORTENER_DEFAULT_REDIRECT_TYPE`,
     * `SHORTENER_DEFAULT_EXPIRY_SECONDS`,
     * `SHORTENER_GENERATED_SLUG_LENGTH`,
     * `SHORTENER_ALLOW_PRIVATE_TARGETS`, and
     * `URL_SHORTENER_STORAGE_CONFIG` (the storage configuration YAML
     * path, equivalent to `--storage-config`); a CLI flag overrides the
     * value set by its corresponding env var when both are present.
     *
     * When `argv[1]` is `"link"`, link-subcommand mode is entered and
     * all server-flag parsing is skipped.
     *
     * @param argc Argument count as received by `main`.
     * @param argv Null-terminated argument vector as received by `main`.
     * @return A `ParseResult` whose fields are populated as follows:
     *   - `config` holds the parsed server configuration; may be only
     *     partially populated when `help_requested` is `true`.
     *   - `help_requested` is `true` and `help_text` is populated when
     *     `--help`, `-h`, `link --help`, `link -h`, or
     *     `link <verb> --help` is present; callers must check this
     *     before using `config` or `command`.
     *   - `command` holds the parsed `LinkCliCommand` for a
     *     `link <verb>` invocation (without `--help`); `std::nullopt`
     *     in server mode.
     * @throws std::invalid_argument On an unrecognized flag, an invalid
     * flag value (bad boolean, out-of-range port, unknown redirect-type
     * token), a missing or unrecognized `link` verb, or a missing or
     * malformed verb flag.
     * @throws std::runtime_error If `SHORTENER_DEFAULT_EXPIRY_SECONDS`
     * or `SHORTENER_GENERATED_SLUG_LENGTH` cannot be parsed as a valid
     * unsigned integer or exceeds the `uint32_t` range.
     */
    ParseResult parse(int argc, char* argv[]) const;
};
