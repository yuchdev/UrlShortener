/**
 * @file link_command_dispatch.hpp
 * @brief One-shot CLI dispatch entry point for `link <verb>` commands.
 *
 * This header is intentionally decoupled from the HTTP server: it forward
 * declares the only two types it needs and pulls in nothing under
 * `url_shortener/http/`. CLI mode must never start the network server or load
 * `uri.txt` (see docs/roadmap/0003-cli_rest_interfaces/plan.md contracts C4
 * and C5).
 */
#pragma once

// Re-exported so callers that include only this header (`src/main.cpp`,
// `tests/unit/cli/04_cli_exit_codes.cpp`) still reach `ExitCodeForAppError`.
// The bridge lives in its own TU so `link_command_dispatch.cpp` need never name
// `app::AppErrorCode` (Task 04.0 exit criterion 2).
#include <url_shortener/cli/app_exit_code.hpp>

/// Top-level server configuration (defined in url_shortener/core/config.hpp).
struct ServerConfig;

/// Recognized `link <verb>` invocation and its parsed argument DTO
/// (defined in url_shortener/cli/cli_parser.hpp).
struct LinkCliCommand;

namespace url_shortener::cli
{
/**
 * @brief Execute a parsed `link <verb>` command in one-shot CLI mode.
 *
 * Invoked from `main()` when a leading `link <verb>` positional was recognized.
 * It converts the parsed DTO to an `args` object (subtask 14) and executes it
 * through the one shared @ref url_shortener::commands::CommandRegistry as the
 * local operator (`ControlSet::Admin`), so CLI sugar runs the same
 * descriptor/handler as `cmd`, REST and the TUI. Before executing it initializes
 * one-shot logging as disabled (decision log D14) so the runtime's audit sink
 * never leaks a `command_audit` line to stderr. It runs entirely in the calling
 * thread and never constructs an @c HttpServer, a Boost.Asio @c io_context, or
 * touches @c UriMapSingleton / `uri.txt` (milestone contracts C4/C5).
 *
 * On success it writes exactly one line to stdout - the handler's `result_json`
 * (the shared REST serializer's output) plus a newline - and returns 0. On
 * failure stdout stays empty, a `"<verb> failed: <message>\n"` diagnostic is
 * written to stderr, and the exit code is @ref ExitCodeForAppError's shared-table
 * value for the error.
 *
 * @param command Parsed link command selecting the verb and its argument DTO.
 * @param config  Server configuration borrowed by reference; supplies the
 * link-service wiring inputs (base domain, slug length, private-target policy)
 * when no process-wide runtime is installed. Must outlive the call.
 * @return 0 on success; on failure the shared-table exit code for the error:
 * 1 for `not_found`; 2 for `invalid_url`, `invalid_slug`, `invalid_field`, or
 * `reserved_slug`; 3 for `slug_conflict`; 4 for `storage_failure`, `internal`,
 * or a runtime-acquisition failure.
 */
int DispatchLinkCommand(const LinkCliCommand& command,
                        const ServerConfig& config);

}  // namespace url_shortener::cli
