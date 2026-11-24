/**
 * @file link_command_dispatch.cpp
 * @brief One-shot CLI dispatch for `link <verb>` commands, routed through the
 * shared command registry (Task 04.0 subtask 15).
 *
 * Each `link <verb>` keeps its hand-written flag grammar and its shared `app::`
 * DTO, but no longer calls the link command service directly: the DTO is
 * converted to an `args` object (subtask 14) and executed through the one
 * @ref url_shortener::commands::CommandRegistry (milestone contract C11), so CLI
 * sugar, `cmd`, REST and the TUI all run the same descriptor/handler. stdout,
 * stderr text and exit codes stay byte-identical to what the CLI documents today
 * (milestone contract C4).
 *
 * IMPORTANT (milestone contracts C4/C5): this translation unit must never start
 * the HTTP server, construct a Boost.Asio @c io_context, or touch
 * @c UriMapSingleton / `uri.txt`. It therefore includes nothing under
 * `url_shortener/http/` and no `boost/asio` headers. Keep it that way; the
 * constraint is an architecture invariant verified by grep during review.
 */
#include <url_shortener/cli/link_command_dispatch.hpp>

#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/command_runtime_acquire.hpp>
#include <url_shortener/cli/link_args_json.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/security/control_set.hpp>

namespace url_shortener::cli
{
namespace
{
/// Human-readable token for a link verb, used only for error diagnostics. Kept
/// (not derived from `command_name_for`) because the stderr line the CLI has
/// always emitted is `"<verb> failed: ..."` with the bare verb, not the dotted
/// registry name (milestone contract C4).
std::string_view verb_token(LinkCliVerb verb)
{
    switch (verb) {
    case LinkCliVerb::create:
        return "create";
    case LinkCliVerb::get:
        return "get";
    case LinkCliVerb::update:
        return "update";
    case LinkCliVerb::del:
        return "delete";
    case LinkCliVerb::enable:
        return "enable";
    case LinkCliVerb::disable:
        return "disable";
    case LinkCliVerb::restore:
        return "restore";
    case LinkCliVerb::preview:
        return "preview";
    case LinkCliVerb::stats:
        return "stats";
    }
    return "unknown";
}

/**
 * @brief Builds the `args` object JSON for a parsed `link <verb>` command.
 *
 * Selects the subtask-14 converter matching @p command.verb and applies it to
 * the DTO alternative the parser stored in @p command.payload. The converters
 * carry no validation beyond choosing JSON types (contract C1); every domain and
 * shape rule stays in the registry's `validate_args` and in the link command
 * service. `enable`/`disable`/`delete`/`restore` share
 * @ref slug_args_json, so the enable/disable boolean rides on the command name
 * (`link.enable` vs `link.disable`), not on `args`.
 *
 * @param command The parsed command; its `payload` alternative is selected by
 * `command.verb` and read with `std::get` (the parser guarantees the matching
 * alternative is engaged).
 * @return The single well-formed `args` object text for the verb's descriptor.
 */
std::string args_json_for(const LinkCliCommand& command)
{
    switch (command.verb) {
    case LinkCliVerb::create:
        return create_args_json(
            std::get<app::CreateLinkCommand>(command.payload));
    case LinkCliVerb::get:
    case LinkCliVerb::preview:
        return get_args_json(std::get<app::GetLinkQuery>(command.payload));
    case LinkCliVerb::update:
        return update_args_json(
            std::get<app::UpdateLinkCommand>(command.payload));
    case LinkCliVerb::del:
        return slug_args_json(
            std::get<app::DeleteLinkCommand>(command.payload).slug);
    case LinkCliVerb::enable:
    case LinkCliVerb::disable:
        return slug_args_json(
            std::get<app::SetLinkEnabledCommand>(command.payload).slug);
    case LinkCliVerb::restore:
        return slug_args_json(
            std::get<app::RestoreLinkCommand>(command.payload).slug);
    case LinkCliVerb::stats:
        return stats_args_json(
            std::get<app::GetLinkStatsQuery>(command.payload));
    }
    // UNREACHABLE: LinkCliVerb is a closed enum exhaustively handled above.
    return "{}";
}
}  // namespace

int DispatchLinkCommand(const LinkCliCommand& command,
                        const ServerConfig& config)
{
    // D14 / threat model T-80: initialize one-shot logging as *disabled* before
    // the runtime executes anything, so the registry's LoggerAuditSink (resolved
    // lazily at emit time) never prints a `command_audit` line to stderr for a
    // successful mutating verb. `link` verbs take no `--log-file`/`--verbose`
    // flags (C4); an operator who needs the audit trail uses `cmd ... --log-file`.
    observability::LoggerFactory::Initialize(
        one_shot_logging_config(OneShotLogging {}));

    const std::string_view verb = verb_token(command.verb);

    // The single shared registry (milestone contract C11), obtained through the
    // helper shared with the `cmd` dispatcher: the process-wide runtime `main`
    // installed from the active backend, or a fallback built from the legacy
    // in-memory default. The fallback and the installed runtime both bind the
    // link service to the process-wide store, so a `create` then `get` in one
    // process observe the same link (the unit tests rely on this).
    const std::shared_ptr<composition::CommandRuntime> runtime =
        acquire_command_runtime(config);
    if (!runtime) {
        std::cerr << verb << " failed: internal\n";
        // Same exit code the shared table assigns `internal` (01.0/02), read
        // straight from the commands table so this unit stays free of the
        // application error enum (Task 04.0 exit criterion 2).
        return commands::error_info_for(commands::CommandErrorCode::internal)
            .exit_code;
    }

    // Convert the parsed DTO to the descriptor's `args` object and execute it as
    // the local operator. Admin because `link` verbs have no `--read-only`
    // (C4/T-81); the string fields are non-identifying audit labels only (T-84).
    commands::CommandRequest request;
    request.v = 1;
    request.id = "cli-link";
    request.command = std::string(command_name_for(command.verb));
    request.args_json = args_json_for(command);

    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.control_set = ControlSet::Admin;
    caller.transport = "cli";

    const commands::CommandResponse response =
        runtime->registry->execute(caller, request);

    if (response.ok) {
        // Exactly one line on stdout: the handler's `result_json` (the shared
        // REST serializer's output, contract C1/C3) plus a trailing newline. A
        // successful command never has an empty `result_json` (the registry maps
        // that to `internal`), so stdout always carries a single JSON object.
        std::cout << response.result_json << '\n';
        return 0;
    }

    // Failure: stdout stays empty; the value-free diagnostic goes to stderr and
    // the exit code comes from the shared table. `response.error.message` is the
    // service's `detail` carried through `to_command_error` unchanged, so the
    // stderr text matches the legacy dispatch byte-for-byte (contract C4).
    std::cerr << verb << " failed: " << response.error.message << '\n';
    return commands::error_info_for(response.error.code).exit_code;
}

}  // namespace url_shortener::cli
