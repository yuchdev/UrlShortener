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

/// Top-level server configuration (defined in url_shortener/core/config.h).
struct ServerConfig;

/// Recognized `link <verb>` invocation and its parsed argument DTO
/// (defined in url_shortener/cli/cli_parser.h).
struct LinkCliCommand;

namespace url_shortener::cli
{
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
