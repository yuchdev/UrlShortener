/**
 * @file command_runtime_acquire.hpp
 * @brief Shared one-shot runtime acquisition for the CLI command dispatchers.
 *
 * Both one-shot CLI modes - the generic `cmd <name>` dispatcher (subtask 13)
 * and the `link <verb>` sugar dispatcher (subtask 15) - need the same single
 * @ref url_shortener::commands::CommandRegistry (milestone contract C11). This
 * header declares the one helper both use so neither duplicates the
 * installed-then-fallback acquisition logic.
 *
 * Like the dispatcher headers, this one is deliberately decoupled from the HTTP
 * server: it forward declares the only type it needs and pulls in nothing under
 * `url_shortener/http/`, no Boost.Asio/Beast, and never starts the network
 * server or touches `uri.txt`/`UriMapSingleton` (milestone contract C5).
 */
#pragma once

#include <memory>

/// Top-level server configuration (defined in url_shortener/core/config.hpp).
struct ServerConfig;

namespace url_shortener
{
namespace composition
{
struct CommandRuntime;
}  // namespace composition

namespace cli
{
/**
 * @brief Obtains the shared command runtime, preferring the installed one.
 *
 * Mirrors the REST bridge's `request_runtime()`: use the process-wide runtime
 * `main` installed from the active backend (so reads and writes share one store
 * under `--storage-config`), falling back to a runtime built here from an
 * inactive backend handle when none is installed (unit tests/tools). Decision
 * log D14 holds either way because the audit `Logger` resolves its sink lazily
 * at emit time, and logging is initialized before any command executes.
 *
 * @param config Server configuration the fallback runtime borrows by reference;
 * must outlive the returned runtime (the link service and lister hold a
 * reference into it). Ignored when a process-wide runtime is already installed.
 * @return The shared runtime, or a null `std::shared_ptr` when no runtime is
 * installed and the fallback build threw (the caller reports `internal`, exit
 * 4). Never throws.
 */
std::shared_ptr<composition::CommandRuntime> acquire_command_runtime(
    const ServerConfig& config);

}  // namespace cli
}  // namespace url_shortener
