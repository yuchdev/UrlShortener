/**
 * @file cmd_dispatch.hpp
 * @brief One-shot CLI dispatch entry point for the generic `cmd <name>` mode.
 *
 * This is the CLI adapter of ADR 0002 (frame in -> `execute` -> frame out): it
 * assembles a request envelope from `--json`/stdin/`--arg`, runs it through the
 * one shared @ref url_shortener::commands::CommandRegistry as the local
 * operator, prints the response envelope (or only `result` under `--raw`) and
 * returns the process exit code from the shared error table. There is no
 * per-command code here (milestone contract C1).
 *
 * Like `link_command_dispatch.hpp`, this header is deliberately decoupled from
 * the HTTP server: it forward declares the only two types it needs and pulls in
 * nothing under `url_shortener/http/`, no Boost.Asio/Beast, and never starts the
 * network server or touches `uri.txt`/`UriMapSingleton` (milestone contract C5).
 */
#pragma once

#include <iostream>

/// Top-level server configuration (defined in url_shortener/core/config.hpp).
struct ServerConfig;

/// A recognized `cmd <name> [options]` invocation parsed by subtask 12
/// (defined in url_shortener/cli/cli_parser.hpp).
struct CmdCliCommand;

namespace url_shortener::cli
{
/**
 * @brief Execute one parsed `cmd <name>` invocation in one-shot CLI mode.
 *
 * Invoked from `main()` after the `link <verb>` branch when a leading
 * `cmd <name>` positional was recognized. It runs entirely on the calling
 * thread and never constructs an @c HttpServer, a Boost.Asio @c io_context, or
 * touches @c UriMapSingleton / `uri.txt` (milestone contracts C4/C5).
 *
 * Pipeline (milestone ADR 0002, subtask 13 spec):
 * 1. Initialize one-shot logging from `command.logging` so the runtime's audit
 *    sink is silenced by default and never leaks a `command_audit` line to
 *    stderr (decision log D14, threat model T-80). `--log-file` selects a File
 *    sink, `--verbose` a Stderr sink.
 * 2. Obtain the shared @ref commands::CommandRegistry: the process-wide runtime
 *    `main` installed from the active backend when present (so `link.list`/
 *    `db.rows` read the same store link CRUD writes under `--storage-config`),
 *    otherwise a runtime built here from an inactive backend handle (unit
 *    tests/tools that never ran startup wiring).
 * 3. Assemble the `args` object from `--json`/stdin/`--arg`, bounding stdin and
 *    `--json` by `config.max_request_body_bytes` (threat model T-79).
 * 4. Build the request envelope and execute it as the local operator
 *    (`control_set = Admin`, downgraded to `User` by `--read-only`, threat
 *    model T-81) through @ref commands::CommandRegistry::execute_json.
 * 5. Print the response envelope (or only `result` under `--raw`) and return
 *    @ref commands::exit_code_for.
 *
 * No argument value, `--json` body, or stdin content is ever echoed on stderr
 * or in a log (threat model T-77/T-78). The function never throws: any escaping
 * exception is mapped to `cmd failed: internal` with exit code 4.
 *
 * @param command Parsed `cmd` invocation (command name, inputs, options,
 * logging policy). Treated as untrusted data; its values are never echoed on an
 * error path.
 * @param config Server configuration providing the input-size bounds and the
 * link-service wiring inputs; borrowed by reference and must outlive the call.
 * @param in Stream read only when `command.json_from_stdin` is set; bounded to
 * at most `config.max_request_body_bytes + 1` bytes. Defaults to `std::cin`.
 * @param out Stream the success/response envelope (or `result`) is written to.
 * Defaults to `std::cout`.
 * @param err Stream the value-free failure diagnostics are written to. Defaults
 * to `std::cerr`.
 * @return The process exit code: `0` on success (including an accepted job);
 * otherwise the shared table value from @ref commands::exit_code_for
 * (`1` not_found; `2` bad input / `unknown_command` / `invalid_envelope` /
 * `unsupported`; `3` slug_conflict; `4` storage/internal/busy/job_cancelled;
 * `5` not_authorized). A logging-init failure or an input-shape rejection
 * (oversized stdin, non-object args, bad `--arg`) returns `2`; a runtime wiring
 * throw returns `4`.
 */
int DispatchCmdCommand(const CmdCliCommand& command,
                       const ServerConfig& config,
                       std::istream& in = std::cin,
                       std::ostream& out = std::cout,
                       std::ostream& err = std::cerr);

}  // namespace url_shortener::cli
