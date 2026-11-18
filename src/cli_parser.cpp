/**
 * @file cli_parser.cpp
 * @brief CLI parser implementation for runtime server configuration.
 */
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <boost/program_options.hpp>
#include <url_shortener/cli/arg_coercion.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/link_command_args.hpp>
#include <url_shortener/commands/envelope.hpp>

namespace po = boost::program_options;

namespace
{

/**
 * @brief Parse a textual boolean representation.
 *
 * @param value Input string.
 * @return true Parsed true value.
 * @return false Parsed false value.
 * @throws std::invalid_argument If the value is not a supported boolean token.
 *         The message never echoes @p value, which may be an attacker-supplied
 *         operand, so a malformed boolean cannot leak back through error text
 *         (CWE-209; threat model T-77).
 */
bool parseBool(const std::string& value)
{
    if (value == "1" || value == "true" || value == "on" || value == "yes") {
        return true;
    }
    if (value == "0" || value == "false" || value == "off" || value == "no") {
        return false;
    }
    throw std::invalid_argument(
        "Invalid boolean value. Use true/false, 1/0, on/off, or yes/no.");
}

/**
 * @brief Validate and convert a raw port value.
 *
 * @param value Raw numeric port candidate.
 * @return uint16_t Validated TCP port.
 * @throws std::out_of_range If the port is outside [1, 65535].
 */
uint16_t parsePort(uint32_t value)
{
    if (value == 0 || value > 65535) {
        throw std::out_of_range("Port number " + std::to_string(value)
                                + " is out of valid range [1, 65535].");
    }
    return static_cast<uint16_t>(value);
}

/**
 * @brief Map a `link` subcommand token to its LinkCliVerb enumerator.
 *
 * @param token The verb token (argv[2]), e.g. "create" or "delete".
 * @return LinkCliVerb The matching verb.
 * @throws std::invalid_argument If the token is not a recognized verb.
 */
LinkCliVerb parseLinkVerb(const std::string& token)
{
    if (token == "create") {
        return LinkCliVerb::create;
    }
    if (token == "get") {
        return LinkCliVerb::get;
    }
    if (token == "update") {
        return LinkCliVerb::update;
    }
    if (token == "delete") {
        return LinkCliVerb::del;
    }
    if (token == "enable") {
        return LinkCliVerb::enable;
    }
    if (token == "disable") {
        return LinkCliVerb::disable;
    }
    if (token == "restore") {
        return LinkCliVerb::restore;
    }
    if (token == "preview") {
        return LinkCliVerb::preview;
    }
    if (token == "stats") {
        return LinkCliVerb::stats;
    }
    throw std::invalid_argument(
        "Unknown link command: '" + token
        + "'. Valid commands: create, get, update, delete, enable, disable, "
          "restore, preview, stats.");
}

/**
 * @brief Report whether a `--help`/`-h` token appears among link-scoped tokens.
 *
 * Mirrors the top-level parser's `--help,h` spelling so `link --help`,
 * `link -h`, and `link <verb> --help` are all recognized as help requests.
 *
 * @param tokens Tokens after the `link` positional (argv[2..argc-1]).
 * @return true If any token is `--help` or `-h`.
 */
bool linkHelpRequested(const std::vector<std::string>& tokens)
{
    for (const auto& token : tokens) {
        if (token == "--help" || token == "-h") {
            return true;
        }
    }
    return false;
}

/**
 * @brief Render the top-level `link` usage text listing every verb.
 *
 * Emitted for `link --help` (no verb). Points the reader at
 * `link <verb> --help` for per-verb flag detail.
 *
 * @return std::string The multi-line usage listing.
 */
std::string buildLinkHelpText()
{
    return "Usage: url_shortener link <verb> [options]\n\n"
           "Manage short links from the command line. Run\n"
           "`url_shortener link <verb> --help` for a verb's flags.\n\n"
           "Verbs:\n"
           "  create   Create a new short link.\n"
           "  get      Look up a link by slug or id.\n"
           "  update   Update fields of an existing link (PATCH semantics).\n"
           "  delete   Soft-delete a link.\n"
           "  enable   Enable a link.\n"
           "  disable  Disable a link.\n"
           "  restore  Restore a soft-deleted link.\n"
           "  preview  Preview a link's target without redirecting.\n"
           "  stats    Report click statistics for a link.\n\n"
           "Global options (all verbs):\n"
           "  --storage-config <FILE>    Storage configuration YAML "
           "(metadata/cache/\n"
           "                             analytics/rate_limit backends). "
           "Default: "
           "in-memory.\n";
}

/**
 * @brief Render usage text for a single `link <verb>`, describing its flags.
 *
 * The flag spellings mirror the per-verb mappers in
 * `src/cli/link_command_args.cpp` exactly (required vs optional, the
 * three-state `update` flags, and the shared get/preview selector), so the
 * help stays in step with what the parser actually accepts.
 *
 * @param verb The recognized link verb.
 * @return std::string The multi-line usage text for @p verb.
 */
std::string buildLinkVerbHelpText(LinkCliVerb verb)
{
    switch (verb) {
        case LinkCliVerb::create:
            return "Usage: url_shortener link create --url <URL> [options]\n\n"
                   "Create a new short link.\n\n"
                   "Required:\n"
                   "  --url <URL>                Target URL to shorten.\n\n"
                   "Optional:\n"
                   "  --slug <SLUG>              Custom slug for the short "
                   "link.\n"
                   "  --redirect-type <temporary|permanent>\n"
                   "                             Redirect type.\n"
                   "  --expires-at <RFC3339>     Expiry timestamp (RFC3339 "
                   "UTC).\n"
                   "  --enabled <BOOL>           Initial enabled state "
                   "(true/false).\n"
                   "  --tag <TAG>                Tag to attach (repeatable).\n"
                   "  --metadata <KEY=VALUE>     Metadata entry (repeatable).\n"
                   "  --campaign-name|-source|-medium|-term|-content|-id "
                   "<VALUE>\n"
                   "                             Campaign attribution fields.\n"
                   "  --base-domain <URL>        Base domain for the short "
                   "URL.\n"
                   "  --allow-private-targets    Permit private/intranet "
                   "targets.\n";
        case LinkCliVerb::get:
            return "Usage: url_shortener link get (--slug <SLUG> | --id "
                   "<ID>)\n\n"
                   "Look up a link. Exactly one selector is required.\n\n"
                   "  --slug <SLUG>              Look up the link by slug.\n"
                   "  --id <ID>                  Look up the link by id.\n";
        case LinkCliVerb::update:
            return "Usage: url_shortener link update --slug <SLUG> "
                   "[options]\n\n"
                   "Update fields of an existing link. Every optional flag "
                   "uses "
                   "PATCH\nsemantics: omit it to leave the field untouched.\n\n"
                   "Required:\n"
                   "  --slug <SLUG>              Slug of the link to "
                   "update.\n\n"
                   "Optional:\n"
                   "  --enabled <BOOL>           New enabled state "
                   "(true/false).\n"
                   "  --expires-at <RFC3339|clear>\n"
                   "                             Set the expiry, or 'clear' to "
                   "drop "
                   "it.\n"
                   "  --tags <A,B,C>             Replacement tags "
                   "(comma-separated; "
                   "empty\n"
                   "                             value clears all tags).\n"
                   "  --metadata <KEY=VALUE,...> Replacement metadata "
                   "(comma-separated;"
                   "\n"
                   "                             empty value clears all "
                   "metadata).\n"
                   "  --campaign-name|-source|-medium|-term|-content|-id "
                   "<VALUE>\n"
                   "                             Set campaign attribution "
                   "fields.\n"
                   "  --clear-campaign           Clear the campaign (mutually "
                   "exclusive\n"
                   "                             with --campaign-*).\n";
        case LinkCliVerb::del:
            return "Usage: url_shortener link delete --slug <SLUG>\n\n"
                   "Soft-delete a link.\n\n"
                   "  --slug <SLUG>              Slug of the link to delete "
                   "(required).\n";
        case LinkCliVerb::enable:
            return "Usage: url_shortener link enable --slug <SLUG>\n\n"
                   "Enable a link.\n\n"
                   "  --slug <SLUG>              Slug of the link to enable "
                   "(required).\n";
        case LinkCliVerb::disable:
            return "Usage: url_shortener link disable --slug <SLUG>\n\n"
                   "Disable a link.\n\n"
                   "  --slug <SLUG>              Slug of the link to disable "
                   "(required).\n";
        case LinkCliVerb::restore:
            return "Usage: url_shortener link restore --slug <SLUG>\n\n"
                   "Restore a soft-deleted link.\n\n"
                   "  --slug <SLUG>              Slug of the link to restore "
                   "(required).\n";
        case LinkCliVerb::preview:
            return "Usage: url_shortener link preview (--slug <SLUG> | --id "
                   "<ID>)\n\n"
                   "Preview a link's target without redirecting. Exactly one "
                   "selector\nis required.\n\n"
                   "  --slug <SLUG>              Look up the link by slug.\n"
                   "  --id <ID>                  Look up the link by id.\n";
        case LinkCliVerb::stats:
            return "Usage: url_shortener link stats --slug <SLUG> --from "
                   "<EPOCH> "
                   "--to <EPOCH> --bucket <BUCKET>\n\n"
                   "Report click statistics for a link. All flags are "
                   "required.\n\n"
                   "  --slug <SLUG>              Slug of the link to report "
                   "on.\n"
                   "  --from <EPOCH>             Window start as a Unix "
                   "epoch.\n"
                   "  --to <EPOCH>               Window end as a Unix epoch.\n"
                   "  --bucket <BUCKET>          Aggregation granularity: "
                   "hour, day, "
                   "or week.\n";
    }
    // Unreachable: every LinkCliVerb enumerator is handled above.
    return buildLinkHelpText();
}

/**
 * @brief Extract the global `--storage-config` flag from a verb's argument
 * list, recording its value in @p config.
 *
 * Scans @p args for `--storage-config <FILE>` or `--storage-config=<FILE>` in
 * any position, erases the matched token(s), and stores the path verbatim in
 * `config.storage_config_path` (no `~` expansion, existence check, or
 * canonicalization — the bootstrap performs those). Removing the flag here
 * keeps it invisible to the strict per-verb parsers, which reject unknown
 * flags. Reused by future one-shot modes beyond `link <verb>`.
 *
 * Performs no filesystem I/O. Error messages name only the flag and never echo
 * the path value, which may reveal filesystem layout.
 *
 * @param args Verb-scoped tokens (the flags following `link <verb>`); the
 *        matched `--storage-config` token(s) are erased in place when the flag
 *        is present and left unchanged otherwise.
 * @param config Destination configuration; `storage_config_path` is set to the
 *        flag value when the flag is present and left untouched otherwise.
 * @throws std::invalid_argument If the flag appears more than once, its value
 *         is missing (the flag is the last token or is immediately followed by
 *         another `--`-prefixed token), or its value is empty.
 */
void extract_storage_config_flag(std::vector<std::string>& args,
                                 ServerConfig& config)
{
    static const std::string flag = "--storage-config";
    static const std::string flag_eq = flag + "=";

    bool found = false;
    std::string value;

    for (std::size_t i = 0; i < args.size();) {
        const std::string token = args[i];
        if (token == flag) {
            if (found) {
                throw std::invalid_argument("Repeated --storage-config flag.");
            }
            if (i + 1 >= args.size() || args[i + 1].rfind("--", 0) == 0) {
                throw std::invalid_argument(
                    "Missing value for --storage-config flag.");
            }
            value = args[i + 1];
            if (value.empty()) {
                throw std::invalid_argument(
                    "Empty value for --storage-config flag.");
            }
            found = true;
            args.erase(args.begin() + i, args.begin() + i + 2);
            continue;
        }
        if (token.rfind(flag_eq, 0) == 0) {
            if (found) {
                throw std::invalid_argument("Repeated --storage-config flag.");
            }
            value = token.substr(flag_eq.size());
            if (value.empty()) {
                throw std::invalid_argument(
                    "Empty value for --storage-config flag.");
            }
            found = true;
            args.erase(args.begin() + i);
            continue;
        }
        ++i;
    }

    if (found) {
        config.storage_config_path = value;
    }
}

/**
 * @brief Parse a `link <verb> [options]` invocation into a LinkCliCommand.
 *
 * Recognizes the verb (argv[2]) and maps the remaining flags into the matching
 * `app::` DTO via the per-verb `cli::parse*Args` mappers. Every verb (create,
 * get, update, delete, enable, disable, restore, preview, stats) has its
 * per-flag mapping fully implemented.
 *
 * @param argc Argument count.
 * @param argv Argument vector (argv[1] is known to be "link").
 * @param config ServerConfig receiving create-time overrides (`--base-domain`,
 *        `--allow-private-targets`).
 * @return LinkCliCommand The recognized verb and its parsed payload.
 * @throws std::invalid_argument If the verb is missing/unrecognized or a
 *         mapped verb's flags are missing, unknown, or malformed.
 */
LinkCliCommand parseLinkCommand(int argc, char* argv[], ServerConfig& config)
{
    namespace app = url_shortener::app;
    namespace cli = url_shortener::cli;

    if (argc < 3) {
        throw std::invalid_argument(
            "Missing link subcommand. Expected: link <verb> [options].");
    }

    LinkCliCommand command;
    command.verb = parseLinkVerb(argv[2]);

    // Flags that follow the verb, e.g. everything after `link create`.
    std::vector<std::string> verb_args(argv + 3, argv + argc);

    // The global --storage-config flag may appear in any position; strip it
    // (recording the path in config) before the strict per-verb parsers run,
    // so they never see it yet still reject genuinely unknown flags.
    extract_storage_config_flag(verb_args, config);

    switch (command.verb) {
        case LinkCliVerb::create:
            command.payload = cli::parseCreateArgs(verb_args, config);
            break;
        case LinkCliVerb::get:
            command.payload = cli::parseGetArgs(verb_args);
            break;
        case LinkCliVerb::update:
            command.payload = cli::parseUpdateArgs(verb_args);
            break;
        case LinkCliVerb::del:
            command.payload = cli::parseDeleteArgs(verb_args);
            break;
        case LinkCliVerb::enable:
            command.payload = cli::parseSetEnabledArgs(verb_args, true);
            break;
        case LinkCliVerb::disable:
            command.payload = cli::parseSetEnabledArgs(verb_args, false);
            break;
        case LinkCliVerb::restore:
            command.payload = cli::parseRestoreArgs(verb_args);
            break;
        case LinkCliVerb::preview:
            // Preview reuses the get-by-slug/id query shape.
            command.payload = cli::parsePreviewArgs(verb_args);
            break;
        case LinkCliVerb::stats:
            command.payload = cli::parseStatsArgs(verb_args);
            break;
    }

    return command;
}

/**
 * @brief Render the static usage text for `url_shortener cmd --help`.
 *
 * Emitted only for `cmd --help` with no command name; `cmd <name> --help`
 * instead sets @ref CmdCliCommand::help so the dispatcher (subtask 13) can
 * print the descriptor's own help. Lists every accepted option and one
 * example. No supplied value is ever interpolated.
 *
 * @return std::string The multi-line usage listing.
 */
std::string buildCmdHelpText()
{
    return "Usage: url_shortener cmd <name> [options]\n\n"
           "Run one registered command and print its JSON envelope, then "
           "exit.\n"
           "The command name is a dotted identifier such as link.create.\n\n"
           "Arguments (choose one source; --json and --arg are mutually "
           "exclusive):\n"
           "  --json <TEXT>        Args as a JSON object. Use --json - to "
           "read\n"
           "                       it from stdin (keeps secrets out of argv).\n"
           "  --arg <NAME=VALUE>   One typed argument (repeatable, order "
           "preserved).\n\n"
           "Options:\n"
           "  --id <TEXT>          Correlation id echoed in the response.\n"
           "  --raw                Print only the envelope's result field.\n"
           "  --dry-run            Validate without applying "
           "(options.dry_run).\n"
           "  --async              Request asynchronous execution "
           "(options.async).\n"
           "  --no-wait            Do not wait for an asynchronous job.\n"
           "  --read-only          Run the local session as a read-only user.\n"
           "  --log-file <PATH>    Write logs to a file (mutually exclusive "
           "with\n"
           "                       --verbose). Default: logging disabled.\n"
           "  --verbose            Write logs to stderr.\n"
           "  --base-domain <URL>  Base domain for generated short URLs.\n"
           "  --allow-private-targets <BOOL>\n"
           "                       Permit private/intranet targets "
           "(true/false).\n"
           "  --storage-config <FILE>\n"
           "                       Storage configuration YAML. Default: "
           "in-memory.\n"
           "  --help, -h           Print this message (cmd <name> --help "
           "prints "
           "the\n"
           "                       command's own help).\n\n"
           "Example:\n"
           "  url_shortener cmd link.create --json '{\"target_url\":"
           "\"https://example.com\"}'\n";
}

/**
 * @brief Read the value token that follows a value-taking `cmd` flag.
 *
 * Mirrors @ref extract_storage_config_flag's rule: the value is the next token,
 * and it is considered missing when @p flag is the last token or is immediately
 * followed by another `--`-prefixed token. The index is advanced past the
 * consumed value. Error messages name only the flag, never the value.
 *
 * @param tokens The `cmd`-scoped token list.
 * @param i In/out index of @p flag; advanced to the consumed value on success.
 * @param flag The flag spelling, used only in the error message.
 * @return const std::string& The value token (borrowed from @p tokens).
 * @throws std::invalid_argument If no value token follows @p flag.
 */
const std::string& cmdFlagValue(const std::vector<std::string>& tokens,
                                std::size_t& i,
                                const char* flag)
{
    if (i + 1 >= tokens.size() || tokens[i + 1].rfind("--", 0) == 0) {
        throw std::invalid_argument(std::string("Missing value for ") + flag
                                    + " flag.");
    }
    return tokens[++i];
}

/**
 * @brief Parse a `cmd <name> [options]` invocation into a CmdCliCommand.
 *
 * Runs a single token loop (no Boost.ProgramOptions, like `link <verb>`): the
 * first bare token is the command name; every other token is a known flag or an
 * error. The global `--storage-config` flag is stripped first (recorded in @p
 * config). Values of `--json`/`--arg` are stored verbatim and never echoed in
 * an error message (threat model T-77). Nothing is executed and the registry is
 * not consulted; only the name *shape* and the simple cross-flag rules are
 * checked here.
 *
 * When `--help`/`-h` appears with no command name, the returned command has an
 * empty @ref CmdCliCommand::command and @ref CmdCliCommand::help set, which the
 * caller turns into the static usage text; otherwise a missing name is an
 * error.
 *
 * @param tokens The tokens after the leading `cmd` positional (argv[2..]);
 *        consumed in place by `--storage-config` stripping.
 * @param config ServerConfig receiving `--base-domain`,
 *        `--allow-private-targets` and `--storage-config`.
 * @return CmdCliCommand The parsed one-shot command (empty name only in the
 *         help-with-no-name case).
 * @throws std::invalid_argument If the name is missing (without `--help`),
 *         malformed in shape, or too long; an option is unknown or missing its
 *         value; `--json` is repeated, empty, or combined with `--arg`; `--arg`
 *         lacks `NAME=VALUE` or has an empty name; `--id` is empty or longer
 *         than `config.request_id_max_length`; `--log-file` is empty or
 *         combined with `--verbose`; or a second positional follows the name.
 */
CmdCliCommand parseCmdCommand(std::vector<std::string>& tokens,
                              ServerConfig& config)
{
    namespace cli = url_shortener::cli;
    namespace commands = url_shortener::commands;

    // Strip the global --storage-config flag (any position) before the flag
    // loop, exactly as the `link <verb>` path does.
    extract_storage_config_flag(tokens, config);

    CmdCliCommand command;
    bool name_set = false;
    bool json_set = false;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (token == "--help" || token == "-h") {
            command.help = true;
        }
        else if (token == "--json") {
            if (json_set) {
                throw std::invalid_argument("Repeated --json flag.");
            }
            const std::string& value = cmdFlagValue(tokens, i, "--json");
            if (value == "-") {
                command.json_from_stdin = true;
            }
            else if (value.empty()) {
                throw std::invalid_argument("Empty value for --json flag.");
            }
            else {
                command.json_args = value;
            }
            json_set = true;
        }
        else if (token == "--arg") {
            const std::string& value = cmdFlagValue(tokens, i, "--arg");
            cli::arg_pair pair;
            if (!cli::split_arg_pair(value, &pair)) {
                // split_arg_pair fails on a token without '=' (bad_pair) or
                // with an empty name (bad_name); neither echoes the value.
                if (value.find('=') == std::string::npos) {
                    throw std::invalid_argument("--arg requires NAME=VALUE.");
                }
                throw std::invalid_argument("--arg has an empty NAME.");
            }
            command.arg_pairs.emplace_back(std::move(pair.name),
                                           std::move(pair.value));
        }
        else if (token == "--id") {
            const std::string& value = cmdFlagValue(tokens, i, "--id");
            if (value.empty()) {
                throw std::invalid_argument("Empty value for --id flag.");
            }
            if (value.size() > config.request_id_max_length) {
                throw std::invalid_argument("--id is too long.");
            }
            command.request_id = value;
        }
        else if (token == "--raw") {
            command.raw = true;
        }
        else if (token == "--dry-run") {
            command.dry_run = true;
        }
        else if (token == "--async") {
            command.async = true;
        }
        else if (token == "--no-wait") {
            command.no_wait = true;
        }
        else if (token == "--read-only") {
            command.read_only = true;
        }
        else if (token == "--log-file") {
            const std::string& value = cmdFlagValue(tokens, i, "--log-file");
            if (value.empty()) {
                throw std::invalid_argument("Empty value for --log-file flag.");
            }
            command.logging.log_file = value;
        }
        else if (token == "--verbose") {
            command.logging.verbose = true;
        }
        else if (token == "--base-domain") {
            const std::string& value = cmdFlagValue(tokens, i, "--base-domain");
            if (value.empty()) {
                throw std::invalid_argument(
                    "Empty value for --base-domain flag.");
            }
            config.shortener_base_domain = value;
        }
        else if (token == "--allow-private-targets") {
            const std::string& value =
                cmdFlagValue(tokens, i, "--allow-private-targets");
            config.shortener_allow_private_targets = parseBool(value);
        }
        else if (token.rfind("--", 0) == 0) {
            // Flags match by exact token, so the GNU `--opt=value` form is
            // unrecognised here. Echo only the stem before the first '=',
            // never the value, which may be a secret (threat model T-77).
            throw std::invalid_argument("Unknown cmd option: "
                                        + token.substr(0, token.find('=')));
        }
        else if (!name_set) {
            command.command = token;
            name_set = true;
        }
        else {
            throw std::invalid_argument(
                "Unexpected positional argument after the command name.");
        }
    }

    // Cross-flag rules.
    if (json_set && !command.arg_pairs.empty()) {
        throw std::invalid_argument("--json and --arg are mutually exclusive.");
    }
    if (command.logging.log_file.has_value() && command.logging.verbose) {
        throw std::invalid_argument(
            "--log-file and --verbose are mutually exclusive.");
    }

    if (!name_set) {
        // `cmd --help` with no name defers to the caller's static usage; a
        // bare `cmd` (or options with no name) is a usage error.
        if (command.help) {
            return command;
        }
        throw std::invalid_argument(
            "Missing command name. Expected: cmd <name> [options].");
    }

    // Shape-only check (no registry lookup); value is not echoed.
    if (!commands::is_valid_command_name(command.command)
        || command.command.size()
            > commands::envelope_limits {}.max_command_len)
    {
        throw std::invalid_argument(
            "Invalid command name. Expected a dotted name like link.create.");
    }

    return command;
}

/**
 * @brief Render the static usage text for `url_shortener serve-stdio --help`.
 *
 * Documents the NDJSON framing contract (one request/response envelope per
 * line, EOF ends the session), states that logs go to stderr and never stdout
 * (threat model T-102), spells out that the session runs by default as a local
 * admin with file paths enabled (so `link.import`/`link.export` can read and
 * write arbitrary server files) and recommends `--read-only` plus
 * `--no-file-paths` or `--path-root` for untrusted/sidecar input (threat model
 * T-107), and points at the transport guide. No supplied value is ever
 * interpolated.
 *
 * @return std::string The multi-line usage listing.
 */
std::string buildServeStdioHelpText()
{
    return "Usage: url_shortener serve-stdio [options]\n\n"
           "Serve one command per line over stdin/stdout as an NDJSON "
           "transport:\n"
           "one request envelope per input line, one response envelope per\n"
           "output line; EOF on stdin drains in-flight jobs and shuts down. "
           "Logs\n"
           "go to stderr, never stdout. See\n"
           "docs/commands/adding-a-transport.md.\n\n"
           "Security: by default this session runs as a local admin with file\n"
           "paths enabled, so link.import/export can read and write arbitrary\n"
           "server files. For untrusted or sidecar input (e.g. a "
           "webview-driven\n"
           "parent), run with --read-only and either --no-file-paths or\n"
           "--path-root <DIR> to disable or confine `path`.\n\n"
           "Options:\n"
           "  --read-only          Run the local session as a read-only user\n"
           "                       (ControlSet::User) instead of the default "
           "admin.\n"
           "  --no-file-paths      Reject link.import/export `path` arguments\n"
           "                       (recommended for a webview-driven "
           "sidecar).\n"
           "  --path-root <DIR>    Confine link.import/export `path` to this "
           "directory\n"
           "                       (mutually exclusive with --no-file-paths).\n"
           "  --base-domain <URL>  Base domain for generated short URLs.\n"
           "  --allow-private-targets <BOOL>\n"
           "                       Permit private/intranet targets "
           "(true/false).\n"
           "  --storage-config <FILE>\n"
           "                       Storage configuration YAML. Default: "
           "in-memory.\n"
           "  --help, -h           Print this message.\n";
}

/**
 * @brief Report whether @p c separates path components on the current platform.
 *
 * POSIX recognizes only `/`; Windows additionally recognizes `\`.
 *
 * @param c The character to test.
 * @return true if @p c is a path separator, false otherwise.
 */
bool is_path_separator(char c)
{
#ifdef _WIN32
    return c == '/' || c == '\\';
#else
    return c == '/';
#endif
}

/**
 * @brief Normalize and validate a `--path-root` value into an absolute root.
 *
 * The rooted path check in `pair_file_access` compares the lexically normalized
 * user path against the lexically normalized root component-by-component, so a
 * malformed root (relative, `~`-prefixed, containing `.`/`..`, or carrying
 * stray or trailing separators) would either silently widen the confinement or
 * fail every rooted access closed. Normalizing once here, at the only place
 * that produces a `path_root`, keeps that closed subtask untouched and makes
 * `--path-root` a meaningful boundary (threat model T-96).
 *
 * The value must be absolute: a leading `/` on POSIX, or a drive-absolute path
 * such as `C:\dir` on Windows. Any `.` or `..` component and any `~`-prefixed
 * value is rejected. Repeated separators are collapsed and trailing separators
 * are stripped, so `"//srv//r/"` normalizes to `"/srv/r"`; a lone root
 * (`"/"`, or `"C:\"` on Windows) is preserved. The caller rejects an empty
 * value before this runs, so the result is never empty.
 *
 * @param value The non-empty `--path-root` operand.
 * @return The normalized absolute root.
 * @throws std::invalid_argument If @p value is not absolute, is `~`-prefixed,
 *         or contains a `.` or `..` component. The message names only the
 *         `--path-root` flag and never echoes @p value (CWE-209; threat model
 *         T-77).
 */
std::string normalize_path_root(const std::string& value)
{
    // Reject home-relative values: "~" expansion is a shell concern and must
    // never silently resolve to an operator's home directory here.
    if (value.front() == '~') {
        throw std::invalid_argument("--path-root must be an absolute path.");
    }

    // Establish the absolute root prefix and the offset at which components
    // begin; a non-absolute value is a usage error.
    std::string normalized;
    std::size_t start = 0;
#ifdef _WIN32
    const bool drive_absolute = value.size() >= 3
        && std::isalpha(static_cast<unsigned char>(value[0])) != 0
        && value[1] == ':' && is_path_separator(value[2]);
    if (!drive_absolute) {
        throw std::invalid_argument("--path-root must be an absolute path.");
    }
    // Normalize the drive root to a single forward-slash form (e.g. "C:/").
    normalized = std::string(1, value[0]) + ":/";
    start = 3;
#else
    if (!is_path_separator(value.front())) {
        throw std::invalid_argument("--path-root must be an absolute path.");
    }
    normalized = "/";
    start = 1;
#endif

    // Walk the remainder, collapsing separator runs and rejecting `.`/`..`.
    for (std::size_t i = start; i < value.size();) {
        if (is_path_separator(value[i])) {
            ++i;
            continue;
        }
        const std::size_t begin = i;
        while (i < value.size() && !is_path_separator(value[i])) {
            ++i;
        }
        const std::string component = value.substr(begin, i - begin);
        if (component == "." || component == "..") {
            throw std::invalid_argument(
                "--path-root must not contain '.' or '..' components.");
        }
        if (normalized.back() != '/') {
            normalized.push_back('/');
        }
        normalized += component;
    }

    return normalized;
}

/**
 * @brief Parse a `serve-stdio [options]` invocation into ServeStdioCliOptions.
 *
 * Runs a single token loop like the `cmd <name>` path (no Boost.ProgramOptions,
 * no sub-verb): every token is a known flag or an error, and no positional is
 * accepted. The global `--storage-config` flag is stripped first (recorded in
 * @p config); `--base-domain` and `--allow-private-targets` are applied to @p
 * config the same way the `link`/`cmd` paths do. Logging flags are
 * deliberately absent: `serve-stdio` forces logs to stderr (subtask 17), so no
 * option can route them to stdout (threat model T-102) and `--log-file` /
 * `--verbose` are simply unknown flags here. Nothing is executed and no
 * transport is constructed. Error messages echo only a flag's stem before any
 * `'='`, never an operand value (threat model T-77).
 *
 * @param tokens The tokens after the leading `serve-stdio` positional
 *        (argv[2..]); consumed in place by `--storage-config` stripping.
 * @param config ServerConfig receiving `--base-domain`,
 *        `--allow-private-targets` and `--storage-config`.
 * @return ServeStdioCliOptions The parsed hardening options.
 * @throws std::invalid_argument If an option is unknown or missing its value,
 *         `--path-root` is empty, a non-flag positional appears, or
 *         `--no-file-paths` and `--path-root` are combined.
 */
ServeStdioCliOptions parse_serve_stdio(std::vector<std::string>& tokens,
                                       ServerConfig& config)
{
    // Strip the global --storage-config flag (any position) before the flag
    // loop, exactly as the `link <verb>` and `cmd` paths do.
    extract_storage_config_flag(tokens, config);

    ServeStdioCliOptions options;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (token == "--read-only") {
            options.read_only = true;
        }
        else if (token == "--no-file-paths") {
            options.no_file_paths = true;
        }
        else if (token == "--path-root") {
            const std::string& value = cmdFlagValue(tokens, i, "--path-root");
            if (value.empty()) {
                throw std::invalid_argument(
                    "Empty value for --path-root flag.");
            }
            options.path_root = normalize_path_root(value);
        }
        else if (token == "--base-domain") {
            const std::string& value = cmdFlagValue(tokens, i, "--base-domain");
            if (value.empty()) {
                throw std::invalid_argument(
                    "Empty value for --base-domain flag.");
            }
            config.shortener_base_domain = value;
        }
        else if (token == "--allow-private-targets") {
            const std::string& value =
                cmdFlagValue(tokens, i, "--allow-private-targets");
            config.shortener_allow_private_targets = parseBool(value);
        }
        else if (token.rfind("--", 0) == 0) {
            // Flags match by exact token; the GNU `--opt=value` form is unknown
            // here. Echo only the stem before the first '=', never the value.
            throw std::invalid_argument("Unknown serve-stdio option: "
                                        + token.substr(0, token.find('=')));
        }
        else {
            throw std::invalid_argument(
                "Unexpected positional argument for serve-stdio.");
        }
    }

    // Cross-flag rule: confining and disabling `path` at once is contradictory.
    if (options.no_file_paths && options.path_root.has_value()) {
        throw std::invalid_argument(
            "--no-file-paths and --path-root are mutually exclusive.");
    }

    return options;
}

/**
 * @brief Render the static usage text for `url_shortener tui --help`.
 *
 * Documents the flags and states plainly that no server is started (contract
 * C5). No supplied value is ever interpolated (threat model T-77).
 *
 * @return std::string The multi-line usage listing.
 */
std::string buildTuiHelpText()
{
    return "Usage: url_shortener tui [options]\n\n"
           "Launch the interactive terminal UI over the configured storage\n"
           "backend. No HTTP/HTTPS server is started and no socket is bound; "
           "the\n"
           "UI runs the same commands as the REST API through the shared "
           "command\n"
           "registry. Requires an interactive terminal.\n\n"
           "Options:\n"
           "  --storage-config <FILE>\n"
           "                       Storage configuration YAML. Default: "
           "in-memory.\n"
           "  --theme <NAME>       Colour theme: dark, light, or mono "
           "(default: dark).\n"
           "  --read-only          Run the session as a read-only user; write\n"
           "                       controls render disabled, not hidden.\n"
           "  --yes                Skip mutation confirmation dialogs.\n"
           "  --log-file <PATH>    Write logs and audit lines to this file "
           "(created\n"
           "                       0600). Default: ./url_shortener-tui.log.\n"
           "  --help, -h           Print this message.\n";
}

/**
 * @brief Parse a `tui [options]` invocation into TuiCliOptions.
 *
 * Runs a single token loop like the `cmd`/`serve-stdio` paths (no
 * Boost.ProgramOptions, no sub-verb): every token is a known flag or an error,
 * and no positional is accepted - so a stray server flag such as `--http-port`
 * is rejected as an unknown `tui` option rather than silently parsed. The
 * global `--storage-config` flag is stripped first (recorded in @p config).
 * `--theme` is validated against exactly `dark|light|mono` here (the string
 * check keeps this `url_shortener_common` translation unit free of the TUI
 * `parse_theme_name`, which `tui_entry.cpp` applies again). Error messages echo
 * only a flag's stem before any `'='`, never an operand value (threat model
 * T-77).
 *
 * @param tokens The tokens after the leading `tui` positional (argv[2..]);
 *        consumed in place by `--storage-config` stripping.
 * @param config ServerConfig receiving `--storage-config`.
 * @return TuiCliOptions The parsed launch options.
 * @throws std::invalid_argument If an option is unknown or missing its value,
 *         `--theme` is not one of `dark|light|mono`, `--log-file` is empty, or
 *         a non-flag positional appears.
 */
TuiCliOptions parseTuiCommand(std::vector<std::string>& tokens,
                              ServerConfig& config)
{
    // Strip the global --storage-config flag (any position) before the flag
    // loop, exactly as the `link`/`cmd`/`serve-stdio` paths do.
    extract_storage_config_flag(tokens, config);

    TuiCliOptions options;

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& token = tokens[i];

        if (token == "--read-only") {
            options.read_only = true;
        }
        else if (token == "--yes") {
            options.assume_yes = true;
        }
        else if (token == "--theme") {
            const std::string& value = cmdFlagValue(tokens, i, "--theme");
            if (value != "dark" && value != "light" && value != "mono") {
                throw std::invalid_argument(
                    "--theme must be dark, light, or mono.");
            }
            options.theme = value;
        }
        else if (token == "--log-file") {
            const std::string& value = cmdFlagValue(tokens, i, "--log-file");
            if (value.empty()) {
                throw std::invalid_argument("Empty value for --log-file flag.");
            }
            options.log_file = value;
        }
        else if (token.rfind("--", 0) == 0) {
            // Flags match by exact token; the GNU `--opt=value` form is unknown
            // here. Echo only the stem before the first '=', never the value.
            throw std::invalid_argument("Unknown tui option: "
                                        + token.substr(0, token.find('=')));
        }
        else {
            throw std::invalid_argument(
                "Unexpected positional argument for tui.");
        }
    }

    return options;
}

}  // namespace

/**
 * @brief Parse process arguments into a normalized ParseResult.
 *
 * Environment variables are loaded first and can be overridden by CLI flags.
 *
 * @param argc Argument count.
 * @param argv Argument vector.
 * @return ParseResult Parsed configuration and help metadata.
 */
ParseResult CliParser::parse(int argc, char* argv[]) const
{
    ParseResult result;
    ServerConfig& cfg = result.config;

    // Read environment variables before CLI args so explicit CLI overrides
    // them.
    if (const char* env = std::getenv("SHORTENER_BASE_DOMAIN"); env != nullptr)
    {
        cfg.shortener_base_domain = env;
    }
    if (const char* env = std::getenv("SHORTENER_DEFAULT_REDIRECT_TYPE");
        env != nullptr)
    {
        cfg.shortener_default_redirect_type = env;
    }
    if (const char* env = std::getenv("SHORTENER_DEFAULT_EXPIRY_SECONDS");
        env != nullptr)
    {
        try {
            const auto val = std::stoul(env);
            if (val > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("value exceeds uint32 max");
            }
            cfg.shortener_default_expiry_seconds = static_cast<uint32_t>(val);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("SHORTENER_DEFAULT_EXPIRY_SECONDS: ") + e.what());
        }
    }
    if (const char* env = std::getenv("SHORTENER_GENERATED_SLUG_LENGTH");
        env != nullptr)
    {
        try {
            const auto val = std::stoul(env);
            if (val > std::numeric_limits<uint32_t>::max()) {
                throw std::out_of_range("value exceeds uint32 max");
            }
            cfg.shortener_generated_slug_length = static_cast<uint32_t>(val);
        } catch (const std::exception& e) {
            throw std::runtime_error(
                std::string("SHORTENER_GENERATED_SLUG_LENGTH: ") + e.what());
        }
    }
    if (const char* env = std::getenv("SHORTENER_ALLOW_PRIVATE_TARGETS");
        env != nullptr)
    {
        cfg.shortener_allow_private_targets = parseBool(env);
    }
    if (const char* env = std::getenv("URL_SHORTENER_STORAGE_CONFIG");
        env != nullptr)
    {
        // Recorded verbatim; the flag (parsed below for server mode, or by
        // extract_storage_config_flag for link mode) overrides it. No I/O here.
        cfg.storage_config_path = std::string(env);
    }

    // Top-level command dispatch: a leading `link` positional selects the
    // one-shot link-management CLI after shared shortener configuration has
    // been loaded, while server-only option parsing remains untouched.
    if (argc >= 2 && std::string(argv[1]) == "link") {
        // Link-scoped help is a separate, additive concern from the top-level
        // server `--help`: `link --help` (no verb) lists every verb, while
        // `link <verb> --help` describes that verb's flags. Server-mode help
        // (argv[1] != "link") never reaches here and is left byte-identical.
        const std::vector<std::string> link_args(argv + 2, argv + argc);
        if (linkHelpRequested(link_args)) {
            const std::string first =
                argc >= 3 ? std::string(argv[2]) : std::string();
            if (first.empty() || first == "--help" || first == "-h") {
                result.help_text = buildLinkHelpText();
            }
            else {
                // A verb precedes --help: describe just that verb. An
                // unrecognized verb still surfaces the usual parse error.
                result.help_text = buildLinkVerbHelpText(parseLinkVerb(first));
            }
            result.help_requested = true;
            return result;
        }
        result.command = parseLinkCommand(argc, argv, result.config);
        return result;
    }

    // Top-level `cmd <name>` one-shot mode: mirrors the `link` branch (no
    // server-flag parsing). `cmd --help` with no name yields the static usage
    // text; `cmd <name> --help` carries help on the command instead. `cmd` is
    // recognized only as argv[1], so a trailing `cmd` in server mode stays a
    // server-flag error.
    if (argc >= 2 && std::string(argv[1]) == "cmd") {
        std::vector<std::string> cmd_args(argv + 2, argv + argc);
        CmdCliCommand parsed = parseCmdCommand(cmd_args, result.config);
        if (parsed.command.empty()) {
            // Only reachable for `cmd --help` with no name.
            result.help_text = buildCmdHelpText();
            result.help_requested = true;
            return result;
        }
        result.cmd = std::move(parsed);
        return result;
    }

    // Top-level `serve-stdio` one-shot mode: mirrors the `link`/`cmd` branches
    // (no server-flag parsing). `serve-stdio --help`/`-h` yields the static
    // usage text; otherwise the hardening options are returned via
    // `ParseResult::serve_stdio`. `serve-stdio` is recognized only as argv[1],
    // so a trailing `serve-stdio` in server mode stays a server-flag error.
    if (argc >= 2 && std::string(argv[1]) == "serve-stdio") {
        std::vector<std::string> stdio_args(argv + 2, argv + argc);
        for (const auto& token : stdio_args) {
            if (token == "--help" || token == "-h") {
                result.help_text = buildServeStdioHelpText();
                result.help_requested = true;
                return result;
            }
        }
        result.serve_stdio = parse_serve_stdio(stdio_args, result.config);
        return result;
    }

    // Top-level `tui` one-shot mode: mirrors the `link`/`cmd`/`serve-stdio`
    // branches (no server-flag parsing). `tui --help`/`-h` yields the static
    // usage text; otherwise the launch options are returned via
    // `ParseResult::tui`. `tui` is recognized only as argv[1], so a trailing
    // `tui` in server mode stays a server-flag error (contract C4).
    if (argc >= 2 && std::string(argv[1]) == "tui") {
        std::vector<std::string> tui_args(argv + 2, argv + argc);
        for (const auto& token : tui_args) {
            if (token == "--help" || token == "-h") {
                result.help_text = buildTuiHelpText();
                result.help_requested = true;
                return result;
            }
        }
        result.tui = parseTuiCommand(tui_args, result.config);
        return result;
    }

    // ---- Option storage for values that need custom conversion ----
    std::string http_enabled_str;
    std::string http_redirect_to_https_str;
    uint32_t http_port_raw = cfg.http_port;
    uint32_t hsts_max_age_raw = 0;

    std::string tls_enabled_str;
    uint32_t https_port_raw = cfg.tls.port;
    std::string tls_session_tickets_str;
    std::string tls_session_cache_str;
    std::string tls_client_auth_str;

    std::string shortener_allow_private_targets_str;
    std::string analytics_enabled_str;
    std::string command_api_enabled_str;
    std::string command_api_role_str;

    // ---- Build options_description ----
    po::options_description general("General options");
    general.add_options()("help,h", "Print this help message and exit.");

    po::options_description http_opts("HTTP options");
    http_opts.add_options()(
        "http-port",
        po::value<uint32_t>(&http_port_raw)->value_name("PORT"),
        "HTTP listening port (default: 8000).")(
        "http-enabled",
        po::value<std::string>(&http_enabled_str)->value_name("BOOL"),
        "Enable HTTP listener (true/false, default: true).")(
        "http-redirect-to-https",
        po::value<std::string>(&http_redirect_to_https_str)->value_name("BOOL"),
        "Redirect all HTTP requests to HTTPS (true/false, default: false).")(
        "hsts-max-age",
        po::value<uint32_t>(&hsts_max_age_raw)->value_name("SECONDS"),
        "HSTS max-age in seconds (optional; omit to disable HSTS header).");

    po::options_description tls_opts("TLS/HTTPS options");
    tls_opts.add_options()(
        "tls-enabled",
        po::value<std::string>(&tls_enabled_str)->value_name("BOOL"),
        "Enable TLS/HTTPS listener (true/false, default: false).")(
        "https-port",
        po::value<uint32_t>(&https_port_raw)->value_name("PORT"),
        "HTTPS listening port (default: 443).")(
        "tls-cert",
        po::value<std::string>(&cfg.tls.certificate_chain_file)
            ->value_name("PATH"),
        "Path to PEM certificate chain file.")(
        "tls-key",
        po::value<std::string>(&cfg.tls.private_key_file)->value_name("PATH"),
        "Path to PEM private key file.")(
        "tls-key-passphrase",
        po::value<std::string>()->value_name("PASSPHRASE"),
        "Private key passphrase (optional).")(
        "tls-ca-file",
        po::value<std::string>()->value_name("PATH"),
        "Path to CA certificate file (optional).")(
        "tls-ca-path",
        po::value<std::string>()->value_name("PATH"),
        "Path to CA certificate directory (optional).")(
        "tls-min-version",
        po::value<std::string>(&cfg.tls.min_version)
            ->value_name("VERSION")
            ->default_value(cfg.tls.min_version),
        "Minimum TLS version (default: TLS1.2).")(
        "tls-cipher-suites",
        po::value<std::string>(&cfg.tls.cipher_suites)->value_name("SUITES"),
        "TLS 1.3 cipher suites (colon-separated).")(
        "tls-ciphers",
        po::value<std::string>(&cfg.tls.ciphers)->value_name("CIPHERS"),
        "TLS 1.2 cipher list (OpenSSL format).")(
        "tls-curves",
        po::value<std::string>(&cfg.tls.curves)
            ->value_name("CURVES")
            ->default_value(cfg.tls.curves),
        "Elliptic curves (default: X25519:P-256).")(
        "tls-alpn",
        po::value<std::string>(&cfg.tls.alpn)
            ->value_name("ALPN")
            ->default_value(cfg.tls.alpn),
        "ALPN protocol (default: http/1.1).")(
        "tls-session-tickets",
        po::value<std::string>(&tls_session_tickets_str)->value_name("BOOL"),
        "Enable TLS session tickets (true/false, default: false).")(
        "tls-session-cache",
        po::value<std::string>(&tls_session_cache_str)->value_name("BOOL"),
        "Enable TLS session cache (true/false, default: true).")(
        "tls-client-auth",
        po::value<std::string>(&tls_client_auth_str)->value_name("MODE"),
        "Client authentication mode: none, optional, or required "
        "(default: none).");

    po::options_description shortener_opts("URL shortener options");
    shortener_opts.add_options()(
        "shortener-base-domain",
        po::value<std::string>(&cfg.shortener_base_domain)->value_name("URL"),
        "Base domain used in generated short URLs "
        "(default: http://localhost:8000; overridable via "
        "SHORTENER_BASE_DOMAIN env var).")(
        "shortener-default-redirect-type",
        po::value<std::string>(&cfg.shortener_default_redirect_type)
            ->value_name("TYPE")
            ->default_value(cfg.shortener_default_redirect_type),
        "Default redirect type: temporary or permanent (default: temporary).")(
        "shortener-default-expiry-seconds",
        po::value<uint32_t>()->value_name("SECONDS"),
        "Default expiration window in seconds for new links when expires_at is "
        "omitted.")("shortener-generated-slug-length",
                    po::value<uint32_t>(&cfg.shortener_generated_slug_length)
                        ->value_name("N")
                        ->default_value(cfg.shortener_generated_slug_length),
                    "Length of auto-generated slugs (default: 7).")(
        "shortener-allow-private-targets",
        po::value<std::string>(&shortener_allow_private_targets_str)
            ->value_name("BOOL"),
        "Allow private/intranet target URLs (true/false, default: false).");

    po::options_description storage_opts("Storage options");
    storage_opts.add_options()(
        "storage-config",
        po::value<std::string>()->value_name("FILE"),
        "Path of the storage configuration YAML "
        "(metadata/cache/analytics/rate_limit backends). Default: in-memory.");

    po::options_description analytics_opts("Analytics options");
    analytics_opts.add_options()(
        "analytics-enabled",
        po::value<std::string>(&analytics_enabled_str)->value_name("BOOL"),
        "Enable analytics collection (true/false, default: true).")(
        "analytics-queue-capacity",
        po::value<uint32_t>(&cfg.analytics_queue_capacity)
            ->value_name("N")
            ->default_value(cfg.analytics_queue_capacity),
        "In-memory analytics queue capacity (default: 1024).")(
        "analytics-client-hash-salt",
        po::value<std::string>(&cfg.analytics_client_hash_salt)
            ->value_name("SALT")
            ->default_value(cfg.analytics_client_hash_salt),
        "HMAC salt for client ID hashing (default: dev-analytics-salt).");

    po::options_description request_opts("Request limits");
    request_opts.add_options()(
        "request-id-max-length",
        po::value<uint32_t>(&cfg.request_id_max_length)
            ->value_name("N")
            ->default_value(cfg.request_id_max_length),
        "Maximum accepted X-Request-Id header length (default: 64).")(
        "max-request-body-bytes",
        po::value<uint32_t>(&cfg.max_request_body_bytes)
            ->value_name("BYTES")
            ->default_value(cfg.max_request_body_bytes),
        "Maximum request body size in bytes (default: 65536).")(
        "max-request-target-length",
        po::value<uint32_t>(&cfg.max_request_target_length)
            ->value_name("N")
            ->default_value(cfg.max_request_target_length),
        "Maximum request target (URL path) length (default: 2048).")(
        "link-create-rate-limit",
        po::value<std::uint64_t>(&cfg.link_create_rate_limit)
            ->value_name("N")
            ->default_value(cfg.link_create_rate_limit),
        "Max link creations per client address per window; keyed on the TCP "
        "peer address only, never a header (default: 30; 0 disables the "
        "throttle).")(
        "link-create-rate-window-seconds",
        po::value<std::uint32_t>(&cfg.link_create_rate_window_seconds)
            ->value_name("SECONDS")
            ->default_value(cfg.link_create_rate_window_seconds),
        "Window length in seconds for --link-create-rate-limit; must be > 0 "
        "(default: 60).");

    po::options_description command_api_opts("Command API options");
    command_api_opts.add_options()(
        "command-api-enabled",
        po::value<std::string>(&command_api_enabled_str)->value_name("BOOL"),
        "Enable the opt-in generic command API and gated /api/v1 routes; "
        "off by default. Not read from the environment (true/false, "
        "default: false).")(
        "command-api-role",
        po::value<std::string>(&command_api_role_str)->value_name("ROLE"),
        "Role for gated command-API callers: user (read-only) or admin. "
        "admin is honoured only when the TCP peer is loopback; behind a "
        "same-host reverse proxy every request is loopback, so keep user. "
        "X-Forwarded-For is not honoured (default: user).");

    // Positional: bare integer argument sets http-port.
    po::options_description positional_hidden;
    positional_hidden.add_options()(
        "positional-port",
        po::value<uint32_t>(&http_port_raw)->value_name("PORT"),
        "Positional port shorthand (sets http-port).");

    po::positional_options_description pos;
    pos.add("positional-port", 1);

    po::options_description all;
    all.add(general)
        .add(http_opts)
        .add(tls_opts)
        .add(shortener_opts)
        .add(storage_opts)
        .add(analytics_opts)
        .add(request_opts)
        .add(command_api_opts)
        .add(positional_hidden);

    po::options_description visible;
    visible.add(general)
        .add(http_opts)
        .add(tls_opts)
        .add(shortener_opts)
        .add(storage_opts)
        .add(analytics_opts)
        .add(request_opts)
        .add(command_api_opts);

    // ---- Parse ----
    po::variables_map vm;
    try {
        po::store(po::command_line_parser(argc, argv)
                      .options(all)
                      .positional(pos)
                      .run(),
                  vm);
    } catch (const po::error& e) {
        throw std::invalid_argument(e.what());
    }

    // ---- Handle --help before notify() ----
    if (vm.count("help")) {
        std::ostringstream oss;
        oss << "Usage: simple-http [OPTIONS] [PORT]\n\n" << visible;
        result.help_text = oss.str();
        result.help_requested = true;
        return result;
    }

    try {
        po::notify(vm);
    } catch (const po::error& e) {
        throw std::invalid_argument(e.what());
    }

    // ---- Convert and validate ----

    // HTTP port (from named --http-port or positional)
    if (vm.count("http-port") || vm.count("positional-port")) {
        cfg.http_port = parsePort(http_port_raw);
    }

    if (!http_enabled_str.empty()) {
        cfg.http_enabled = parseBool(http_enabled_str);
    }

    if (!http_redirect_to_https_str.empty()) {
        cfg.http_redirect_to_https = parseBool(http_redirect_to_https_str);
    }

    if (vm.count("hsts-max-age")) {
        cfg.hsts_max_age = hsts_max_age_raw;
    }

    // TLS
    if (!tls_enabled_str.empty()) {
        cfg.tls.enabled = parseBool(tls_enabled_str);
    }

    if (vm.count("https-port")) {
        cfg.tls.port = parsePort(https_port_raw);
    }

    if (vm.count("tls-key-passphrase")) {
        cfg.tls.private_key_passphrase =
            vm["tls-key-passphrase"].as<std::string>();
    }

    if (vm.count("tls-ca-file")) {
        cfg.tls.ca_file = vm["tls-ca-file"].as<std::string>();
    }

    if (vm.count("tls-ca-path")) {
        cfg.tls.ca_path = vm["tls-ca-path"].as<std::string>();
    }

    if (!tls_session_tickets_str.empty()) {
        cfg.tls.session_tickets = parseBool(tls_session_tickets_str);
    }

    if (!tls_session_cache_str.empty()) {
        cfg.tls.session_cache = parseBool(tls_session_cache_str);
    }

    if (!tls_client_auth_str.empty()) {
        cfg.tls.client_auth_mode =
            url_shortener::parseClientAuthMode(tls_client_auth_str);
    }

    if (!shortener_allow_private_targets_str.empty()) {
        cfg.shortener_allow_private_targets =
            parseBool(shortener_allow_private_targets_str);
    }
    if (vm.count("shortener-default-expiry-seconds")) {
        cfg.shortener_default_expiry_seconds =
            vm["shortener-default-expiry-seconds"].as<uint32_t>();
    }

    if (!analytics_enabled_str.empty()) {
        cfg.analytics_enabled = parseBool(analytics_enabled_str);
    }

    // Command API gate (milestone 0006 Task 04.0 subtask 08). Neither flag is
    // read from the environment: a secure-by-default switch must be an explicit
    // argument. --command-api-enabled uses the shared BOOL grammar; invalid
    // tokens surface as std::invalid_argument from parseBool.
    if (!command_api_enabled_str.empty()) {
        cfg.command_api_enabled = parseBool(command_api_enabled_str);
    }
    // --command-api-role accepts exactly "user" or "admin" (case-sensitive);
    // vm.count (not emptiness) is the "was provided" sentinel so an explicit
    // empty value is rejected rather than silently kept as the "user" default.
    if (vm.count("command-api-role")) {
        if (command_api_role_str != "user" && command_api_role_str != "admin") {
            throw std::invalid_argument(
                "--command-api-role must be user or admin");
        }
        cfg.command_api_role = command_api_role_str;
    }

    // The flag overrides any URL_SHORTENER_STORAGE_CONFIG env value above.
    if (vm.count("storage-config")) {
        const auto path = vm["storage-config"].as<std::string>();
        if (path.empty()) {
            throw std::invalid_argument(
                "Empty value for --storage-config flag.");
        }
        cfg.storage_config_path = path;
    }

    // The create throttle window must be a positive number of seconds; a zero
    // window is an invalid fixed-window policy (the limiter would deny every
    // call). The limit itself may be 0 (the documented disable switch).
    if (cfg.link_create_rate_window_seconds == 0) {
        throw std::invalid_argument(
            "--link-create-rate-window-seconds must be greater than 0.");
    }

    return result;
}

url_shortener::observability::LoggingConfig one_shot_logging_config(
    const OneShotLogging& logging)
{
    namespace obs = url_shortener::observability;

    obs::LoggingConfig cfg;
    if (logging.log_file.has_value()) {
        cfg.enabled = true;
        cfg.sink = obs::LogSink::File;
        cfg.file_path = *logging.log_file;
    }
    else if (logging.verbose) {
        cfg.enabled = true;
        cfg.sink = obs::LogSink::Stderr;
    }
    else {
        cfg.enabled = false;
    }
    return cfg;
}
