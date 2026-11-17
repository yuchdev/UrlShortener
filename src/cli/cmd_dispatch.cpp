/**
 * @file cmd_dispatch.cpp
 * @brief One-shot CLI dispatch for the generic `cmd <name>` mode (subtask 13).
 *
 * IMPORTANT (milestone contracts C4/C5): this translation unit must never start
 * the HTTP server, construct a Boost.Asio @c io_context, or touch
 * @c UriMapSingleton / `uri.txt`. It therefore includes nothing under
 * `url_shortener/http/` and no `boost/asio` headers. The constraint is an
 * architecture invariant verified by grep during review (success criterion 2).
 */
#include <url_shortener/cli/cmd_dispatch.hpp>

#include <algorithm>
#include <cstddef>
#include <exception>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <url_shortener/cli/arg_coercion.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/commands/catalog.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/json_scan.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/observability/logger_factory.hpp>

namespace url_shortener::cli
{
namespace
{

/**
 * @brief Writes a value-free pre-execution diagnostic and returns exit 2.
 *
 * Used for the CLI-adapter input rejections that never reach the registry
 * (oversized stdin, non-object `args`, a bad `--arg`): they are reported on
 * @p err with the fixed `cmd failed: <code>: <message>` shape and exit code 2,
 * in both default and `--raw` modes. @p code and @p message are fixed vocabulary
 * from the dispatcher only; no argument value, `--json` body or stdin content is
 * ever passed here (threat model T-77/T-78/T-79).
 *
 * @param err Stream the diagnostic is written to.
 * @param code Fixed error-code token (`invalid_envelope`/`invalid_field`).
 * @param message Fixed, value-free detail.
 * @return Always `2` (bad input), for the caller to return directly.
 */
int reject_input(std::ostream& err, const char* code, const char* message)
{
    err << "cmd failed: " << code << ": " << message << '\n';
    return 2;
}

/**
 * @brief Prints the final command response and returns its process exit code.
 *
 * Realises the output contract (milestone contract C2, subtask 13 step 6):
 * - a job acceptance (no `job_runner` is wired until Task 05.0 subtask 20) is
 *   printed as the accepted envelope with a single stderr note and exit 0;
 * - default mode prints `serialize_response(response)` on @p out;
 * - `--raw` prints only `result` on @p out for a success, or
 *   `cmd failed: <code>: <message>` on @p err for a failure (empty stdout).
 *
 * `response.error.message` is already value-free (milestone contract C6); no
 * argument value or request body ever reaches @p err.
 *
 * @param response The response produced by the registry.
 * @param raw Whether `--raw` was requested.
 * @param out Stream for the success envelope/result.
 * @param err Stream for the value-free failure diagnostic.
 * @return @ref commands::exit_code_for for @p response (0 on success/job).
 */
int print_response(const commands::CommandResponse& response, bool raw,
                   std::ostream& out, std::ostream& err)
{
    if (response.ok && response.job.has_value()) {
        // A job was accepted for background work. Waiting (and the 0/4 waited
        // exit code) is a follow-up edit owned by Task 05.0 subtask 20; until
        // the process-wide job runtime exists, print the accepted envelope and
        // note that waiting is unavailable. exit_code_for returns 0 here.
        out << commands::serialize_response(response) << '\n';
        err << "job accepted; waiting is not available in this build\n";
        return commands::exit_code_for(response);
    }

    if (raw) {
        if (response.ok) {
            out << response.result_json << '\n';
        }
        else {
            err << "cmd failed: "
                << commands::error_info_for(response.error.code).name << ": "
                << response.error.message << '\n';
        }
    }
    else {
        out << commands::serialize_response(response) << '\n';
    }
    return commands::exit_code_for(response);
}

/**
 * @brief Reads the `args` object text from @p in, bounded to the body limit.
 *
 * Reads at most `config.max_request_body_bytes + 1` bytes and never more, so a
 * hostile unbounded pipe cannot exhaust memory (threat model T-79). A read that
 * reaches the `+ 1` byte means the input exceeds the limit and is rejected.
 *
 * The buffer grows in fixed 64 KiB chunks toward the cap as bytes actually
 * arrive, so a large configured cap with a tiny input allocates only what was
 * read, not the full cap up front (CWE-789: memory sized from an untrusted
 * limit).
 *
 * @param in Stream to read the args object from.
 * @param config Server configuration supplying the byte cap.
 * @param out_args On a successful bounded read, set to the raw bytes read.
 * @return `true` when the input fit within the limit; `false` when it exceeded
 * it (the caller rejects with exit 2 and a value-free message).
 */
bool read_stdin_args(std::istream& in, const ServerConfig& config,
                     std::string* out_args)
{
    const std::size_t limit =
        static_cast<std::size_t>(config.max_request_body_bytes);
    const std::size_t cap = limit + 1;  // reading the +1 byte means overflow.

    constexpr std::size_t chunk_size = 64 * 1024;  // 64 KiB read granularity.
    std::vector<char> chunk(chunk_size);
    std::string buffer;

    while (buffer.size() < cap) {
        const std::size_t want = std::min(chunk_size, cap - buffer.size());
        in.read(chunk.data(), static_cast<std::streamsize>(want));
        const std::size_t got = static_cast<std::size_t>(in.gcount());
        buffer.append(chunk.data(), got);
        if (got < want) {
            break;  // EOF or short read: no more input is available.
        }
    }

    if (buffer.size() > limit) {
        return false;
    }
    *out_args = std::move(buffer);
    return true;
}

/**
 * @brief Assembles the `args` object JSON text from the parsed inputs.
 *
 * Follows the precedence of subtask 13 step 3: `--json -` (stdin) wins, then
 * `--json <text>`, then `--arg` coercion against the descriptor's
 * @ref commands::ArgSpec list, otherwise the empty object `"{}"`. Only shape
 * errors are reported here; the registry validates the arg *contents*.
 *
 * @param command The parsed invocation.
 * @param config Server configuration (byte cap for stdin/`--json`).
 * @param descriptor The resolved descriptor, or `nullptr` for an unknown
 * command (coercion then emits every pair as a string so the registry reports
 * it).
 * @param in Stream read only for `--json -`.
 * @param out_args On success, set to the assembled single JSON object text.
 * @param err Stream for a value-free rejection diagnostic.
 * @param out_rc On failure, set to the exit code to return (always 2).
 * @return `true` when `args` was assembled; `false` on an input-shape rejection
 * (oversized stdin or a bad `--arg`), with @p err written and @p out_rc set.
 */
bool assemble_args(const CmdCliCommand& command, const ServerConfig& config,
                   const commands::CommandDescriptor* descriptor,
                   std::istream& in, std::string* out_args, std::ostream& err,
                   int* out_rc)
{
    if (command.json_from_stdin) {
        if (!read_stdin_args(in, config, out_args)) {
            *out_rc = reject_input(err, "invalid_envelope", "args too large");
            return false;
        }
        return true;
    }

    if (command.json_args.has_value()) {
        *out_args = *command.json_args;
        return true;
    }

    if (!command.arg_pairs.empty()) {
        std::vector<arg_pair> pairs;
        pairs.reserve(command.arg_pairs.size());
        for (const auto& kv : command.arg_pairs) {
            pairs.push_back(arg_pair {kv.first, kv.second});
        }

        const std::vector<commands::ArgSpec> empty_specs;
        const std::vector<commands::ArgSpec>& specs =
            descriptor ? descriptor->args : empty_specs;

        std::string coerce_error;
        if (!coerce_args(specs, pairs, out_args, &coerce_error)) {
            // coerce_error is a fixed, value-free token (bad_name,
            // duplicate_name, map_entry_empty, map_entry_duplicate).
            err << "cmd failed: invalid_field: " << coerce_error << '\n';
            *out_rc = 2;
            return false;
        }
        return true;
    }

    *out_args = "{}";
    return true;
}

/**
 * @brief Builds the v1 request-envelope JSON text wrapping @p args_json.
 *
 * The `id` and `command` are JSON-escaped through the shared
 * `url_shortener::jsonString` escaper (no argv byte is ever concatenated raw);
 * `args_json` has already been validated as exactly one JSON object by the
 * caller, so it is embedded verbatim. `options` always carries the two closed
 * boolean flags the envelope permits.
 *
 * @param id Correlation id to echo (validated/escaped on embedding).
 * @param name Dotted command name (shape-checked by the parser; escaped).
 * @param args_json The validated single-object `args` text, embedded verbatim.
 * @param dry_run Value of `options.dry_run`.
 * @param async Value of `options.async`.
 * @return The request-envelope JSON text for @ref execute_json.
 */
std::string build_envelope(const std::string& id, const std::string& name,
                           const std::string& args_json, bool dry_run,
                           bool async)
{
    std::string text = "{\"v\":1,\"id\":";
    text += url_shortener::jsonString(id);
    text += ",\"command\":";
    text += url_shortener::jsonString(name);
    text += ",\"args\":";
    text += args_json;
    text += ",\"options\":{\"dry_run\":";
    text += dry_run ? "true" : "false";
    text += ",\"async\":";
    text += async ? "true" : "false";
    text += "}}";
    return text;
}

/**
 * @brief Obtains the shared command runtime, preferring the installed one.
 *
 * Mirrors the REST bridge's `request_runtime()`: use the process-wide runtime
 * `main` installed from the active backend (so reads and writes share one store
 * under `--storage-config`), falling back to a runtime built here from an
 * inactive backend handle when none is installed (unit tests/tools). D14 holds
 * either way because the audit `Logger` resolves its sink lazily at emit time,
 * and logging is initialized before any command executes.
 *
 * @param config Server configuration the fallback runtime borrows by reference.
 * @return The shared runtime, or `nullptr` when the fallback build threw.
 */
std::shared_ptr<composition::CommandRuntime> acquire_runtime(
    const ServerConfig& config)
{
    if (std::shared_ptr<composition::CommandRuntime> installed =
            composition::process_command_runtime())
    {
        return installed;
    }
    try {
        return std::make_shared<composition::CommandRuntime>(
            composition::build_command_runtime(config,
                                               app::LinkBackendHandle {}));
    }
    catch (const std::exception&) {
        return nullptr;
    }
}

}  // namespace

int DispatchCmdCommand(const CmdCliCommand& command, const ServerConfig& config,
                       std::istream& in, std::ostream& out, std::ostream& err)
{
    try {
        // Step 0: one-shot logging policy (decision log D14). Silenced by
        // default, so the runtime's LoggerAuditSink never prints a
        // command_audit line on stderr (threat model T-80). Done before the
        // runtime executes anything, since the Logger reads its sink lazily.
        std::string log_error;
        if (!observability::LoggerFactory::Initialize(
                one_shot_logging_config(command.logging), &log_error))
        {
            err << "cmd failed: invalid_request: " << log_error << '\n';
            return 2;
        }

        // Step 1: the single shared registry (milestone contract C11).
        const std::shared_ptr<composition::CommandRuntime> runtime =
            acquire_runtime(config);
        if (!runtime) {
            err << "cmd failed: internal\n";
            return 4;
        }
        const commands::CommandRegistry& registry = *runtime->registry;

        const commands::CommandDescriptor* const descriptor =
            registry.find(command.command);

        // Step 2: `<name> --help` prints the descriptor help and executes
        // nothing; an unknown command with `--help` still yields the
        // unknown_command envelope (exit 2) via the execute path below.
        if (command.help && descriptor != nullptr) {
            out << commands::render_command_help(*descriptor);
            return 0;
        }

        // Step 3: assemble the args object (bounded; shape errors -> exit 2).
        std::string args_json;
        if (command.help) {
            // Unknown command + --help: the name resolves to unknown_command
            // regardless of args, so skip input assembly (and stdin).
            args_json = "{}";
        }
        else {
            int rc = 0;
            if (!assemble_args(command, config, descriptor, in, &args_json, err,
                               &rc))
            {
                return rc;
            }
        }

        // Step 4: the args must be exactly one JSON object before it is
        // embedded, which also blocks extra-member smuggling such as
        // `{"a":1},"command":"x"` (threat model T-79). The scanner is the only
        // sanctioned tokenizer and never echoes the input.
        commands::scan_limits args_limits;
        args_limits.max_bytes =
            static_cast<std::size_t>(config.max_request_body_bytes);
        const commands::scan_result scanned =
            commands::scan_object(args_json, args_limits);
        if (!scanned.ok) {
            return reject_input(err, "invalid_envelope",
                                "args must be a JSON object");
        }

        // Step 4 (cont.): build the envelope and execute it as the local
        // operator. Admin by default; `--read-only` downgrades to User so every
        // mutating command fails not_authorized before its handler (T-81).
        commands::CallerContext caller;
        caller.principal = "local-operator";
        caller.control_set =
            command.read_only ? ControlSet::User : ControlSet::Admin;
        caller.transport = "cli";

        const std::string id = command.request_id.value_or("cli-1");
        const std::string envelope =
            build_envelope(id, command.command, args_json, command.dry_run,
                           command.async);

        commands::envelope_limits limits;
        limits.max_bytes =
            static_cast<std::size_t>(config.max_request_body_bytes);
        limits.max_id_len =
            static_cast<std::size_t>(config.request_id_max_length);

        const commands::CommandResponse response =
            registry.execute_json(caller, envelope, limits);

        // Steps 5-6: print the response (or only `result` under `--raw`) and
        // return the shared-table exit code.
        return print_response(response, command.raw, out, err);
    }
    catch (...) {
        // The function never throws (success criterion): any escaping exception
        // is mapped to a value-free internal failure. execute_json is const and
        // documented never to throw, so this is defense in depth.
        err << "cmd failed: internal\n";
        return 4;
    }
}

}  // namespace url_shortener::cli
