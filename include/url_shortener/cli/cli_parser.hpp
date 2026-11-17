/**
 * @file cli_parser.hpp
 * @brief CLI argument parsing contract for url_shortener.
 */
#pragma once

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <url_shortener/app/link_command_service.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/observability/logging_config.hpp>

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

/// Logging policy of the one-shot CLI modes (`cmd`, `link <verb>`), decision
/// log D14.
///
/// A one-shot invocation never starts the server's logging subsystem, so an
/// uninitialized logger would leak its audit lines to stderr (threat model
/// T-80). These two flags select the explicit policy @ref
/// one_shot_logging_config turns into a `LoggingConfig`: silence by default, a
/// file sink with `--log-file`, or a stderr sink with `--verbose`. The two
/// flags are mutually exclusive (rejected at parse time). Shared by the `cmd`
/// parser (subtask 12) and the `cmd`/`link` dispatchers (subtasks 13, 15).
struct OneShotLogging
{
    ///< `--log-file <path>`: enable a File sink writing to this path. Empty or
    ///< combined with @ref verbose is a parse error, so when set the path is
    ///< non-empty. `std::nullopt` means the flag was not given.
    std::optional<std::string> log_file;

    ///< `--verbose`: enable a Stderr sink. Combined with @ref log_file is a
    ///< parse error. `false` means the flag was not given.
    bool verbose = false;
};

/// A recognized `cmd <name> [options]` one-shot invocation, carried verbatim.
///
/// This subtask (12) only turns `argv` into plain strings and booleans; nothing
/// is executed, the registry is not consulted, and `--json`/`--arg` values are
/// stored untouched (their length and JSON validity are checked later by the
/// envelope/registry layer). @ref command is shape-checked against @ref
/// is_valid_command_name only. @ref json_args and @ref arg_pairs are mutually
/// exclusive at the source grammar (a parse error rejects using both).
struct CmdCliCommand
{
    ///< The dotted command name (argv after `cmd`), shape-checked against @ref
    ///< is_valid_command_name; never resolved against the registry here.
    std::string command;

    ///< `--json <text>` body, stored verbatim; never `"-"` (that sets @ref
    ///< json_from_stdin) and never combined with @ref arg_pairs.
    ///< `std::nullopt` when `--json` was not given.
    std::optional<std::string> json_args;

    ///< `true` when `--json -` requested reading the args object from stdin;
    ///< the actual read happens in the dispatcher (subtask 13), not here.
    bool json_from_stdin = false;

    ///< `--arg <name>=<value>` operands in command-line order (first-seen order
    ///< preserved); each stored as the raw name/value halves, unescaped and
    ///< unvalidated. Empty when `--json` was used instead.
    std::vector<std::pair<std::string, std::string>> arg_pairs;

    ///< `--id <text>` correlation id, non-empty and at most
    ///< `ServerConfig::request_id_max_length` bytes. `std::nullopt` when the
    ///< flag was not given.
    std::optional<std::string> request_id;

    ///< `--raw`: print only the envelope's `result` (dispatcher behaviour).
    bool raw = false;

    ///< `--dry-run`: set `options.dry_run` on the request envelope.
    bool dry_run = false;

    ///< `--async`: set `options.async` on the request envelope.
    bool async = false;

    ///< `--no-wait`: do not wait for an asynchronous job to finish.
    bool no_wait = false;

    ///< `--read-only`: run the local session as `ControlSet::User` rather than
    ///< the default `Admin` (ADR 0002 Authorization).
    bool read_only = false;

    ///< `cmd <name> --help`: the dispatcher prints the descriptor's help and
    ///< executes nothing. Never set for `cmd --help` with no name (that sets
    ///< `ParseResult::help_requested` instead).
    bool help = false;

    ///< Logging policy from `--log-file`/`--verbose` (decision log D14).
    OneShotLogging logging;
};

/// Options parsed from a `serve-stdio [options]` one-shot invocation.
///
/// `serve-stdio` runs the NDJSON stdio transport (Task 05.0 subtask 17): one
/// request envelope per stdin line, one response per stdout line, EOF ends the
/// session. This subtask (16) only turns `argv` into this struct; nothing is
/// executed and no transport, `io_context` or `HttpServer` is constructed.
///
/// The three fields harden the session for the case where the parent process
/// relays stdin from a less-trusted producer such as a webview sidecar (threat
/// model T-107). By default the local caller is `ControlSet::Admin` with full
/// filesystem access, so a sidecar deployment is expected to set @ref read_only
/// and one of @ref no_file_paths / @ref path_root.
struct ServeStdioCliOptions
{
    ///< `--read-only`: run the local session as `ControlSet::User` rather than
    ///< the default `Admin` (ADR 0002 Authorization). `false` when the flag was
    ///< not given.
    bool read_only = false;

    ///< `--no-file-paths`: reject `link.import`/`link.export` `path` arguments
    ///< as `unsupported` (decision Q6, recommended for webview-driven
    ///< sidecars). Mutually exclusive with @ref path_root (rejected at parse
    ///< time). `false` when the flag was not given.
    bool no_file_paths = false;

    ///< `--path-root <dir>`: confine `link.import`/`link.export` `path` to this
    ///< directory (05.0/07 rule 6). Stored with any trailing separator stripped
    ///< (except a lone "/"), so the value is never empty and never ends in a
    ///< separator; a trailing separator would otherwise make every in-root path
    ///< fail the lexical root check in `pair_file_access`. The directory's
    ///< existence is validated at startup (subtask 17), not here. Mutually
    ///< exclusive with @ref no_file_paths. `std::nullopt` when the flag was not
    ///< given.
    std::optional<std::string> path_root;
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

    /// Set only for `cmd <name>` invocations; std::nullopt otherwise. Never
    /// engaged at the same time as @ref command (the two top-level modes are
    /// mutually exclusive).
    std::optional<CmdCliCommand> cmd;

    /// Set only for `serve-stdio` invocations; std::nullopt otherwise. Never
    /// engaged at the same time as @ref command or @ref cmd (the top-level
    /// modes are mutually exclusive).
    std::optional<ServeStdioCliOptions> serve_stdio;
};

/**
 * @brief Maps a one-shot logging policy to a concrete `LoggingConfig`.
 *
 * Realises decision log D14 for the `cmd` and `link <verb>` modes: the sink is
 * chosen from the two mutually exclusive flags, never Stdout (which carries the
 * command result).
 *
 * @param logging The parsed `--log-file`/`--verbose` policy.
 * @return A `LoggingConfig` that is disabled when neither flag is set;
 * `{enabled, File, file_path}` when `logging.log_file` is set; or
 * `{enabled, Stderr}` when `logging.verbose` is set. When both are set
 * (a state the parser rejects) the file sink takes precedence.
 */
url_shortener::observability::LoggingConfig one_shot_logging_config(
    const OneShotLogging& logging);

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
 *  - Recognizes a leading `cmd <name>` positional as the generic one-shot
 *    command mode and returns it via `ParseResult::cmd`, likewise
 *    short-circuiting all server-flag parsing.
 *  - Recognizes a leading `serve-stdio` positional as the NDJSON stdio
 *    transport mode and returns its options via `ParseResult::serve_stdio`,
 *    likewise short-circuiting all server-flag parsing.
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
