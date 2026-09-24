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

#include <url_shortener/app/app_error.hpp>

/// Top-level server configuration (defined in url_shortener/core/config.h).
struct ServerConfig;

/// Recognized `link <verb>` invocation and its parsed argument DTO
/// (defined in url_shortener/cli/cli_parser.h).
struct LinkCliCommand;

namespace url_shortener::cli
{
/**
 * @brief Map an application error code to the CLI process exit code.
 *
 * This is the CLI-side counterpart to link_handlers.cpp's
 * `statusForAppError`/`codeForAppError` (which map @ref app::AppErrorCode to an
 * HTTP status and error-code string). It is defined exactly once and every
 * `link <verb>` command routes its failure path through it, so exit codes stay
 * consistent across verbs and a script can branch on `$?` alone.
 *
 * Mapping (see the definition for the rationale of each group):
 * - @c none            -> 0 (not an error path; callers should not reach here)
 * - @c not_found       -> 1
 * - @c invalid_url, @c invalid_slug, @c invalid_field, @c reserved_slug -> 2
 * - @c slug_conflict   -> 3
 * - @c storage_failure, @c internal -> 4
 *
 * @param code Application error code carried by a failed service @c Result.
 * @return Non-negative process exit code (0 only for @c none).
 */
int ExitCodeForAppError(app::AppErrorCode code);

/**
 * @brief Execute a parsed `link <verb>` command in one-shot CLI mode.
 *
 * Invoked from `main()` when a leading `link <verb>` positional was
 * recognized, before any server-mode setup. It runs entirely in the calling
 * thread and never constructs an @c HttpServer, a Boost.Asio @c io_context, or
 * touches @c UriMapSingleton / `uri.txt`: CLI mode must not start the network
 * server or load persisted state (plan.md C4 and C5).
 *
 * @param command Parsed link command selecting the verb and its argument DTO.
 * @param config  Server configuration providing storage/service wiring inputs.
 * @return Process exit code (0 on success, non-zero on failure).
 */
int DispatchLinkCommand(const LinkCliCommand& command,
                        const ServerConfig& config);

}  // namespace url_shortener::cli
