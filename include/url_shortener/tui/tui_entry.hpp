/**
 * @file tui_entry.hpp
 * @brief One-shot process entry for the interactive `tui` mode (Task 06.0
 * subtask 19).
 *
 * `url_shortener tui` launches the FTXUI terminal UI over the configured
 * storage backend. This header declares the three entry points `main()` and the
 * unit tests use; it names no FTXUI type and pulls in nothing under
 * `url_shortener/http/`, no Boost.Asio/Beast, and never starts the network
 * server or touches `uri.txt`/`UriMapSingleton` (milestone contracts C5/C11).
 * The two forward-declared global-namespace types (`ServerConfig`,
 * `TuiCliOptions`) and the forward-declared `app::LinkBackendHandle` keep this
 * seam free of storage/CLI definitions.
 */
#pragma once

#include <optional>
#include <string>

#include <url_shortener/observability/logging_config.hpp>
#include <url_shortener/tui/tui_options.hpp>

/// Top-level server configuration (defined in url_shortener/core/config.hpp).
struct ServerConfig;

/// Parsed `tui` launch options `--read-only` / `--yes` / `--theme` /
/// `--log-file` (defined in url_shortener/cli/cli_parser.hpp).
struct TuiCliOptions;

namespace url_shortener::app
{
/// The process-wide storage handle `main` installed (active under
/// `--storage-config`, inactive for the legacy in-memory default); defined in
/// url_shortener/composition/link_backend_bootstrap.hpp. Taken by const
/// reference so this seam header needs no storage definitions.
class LinkBackendHandle;
}  // namespace url_shortener::app

namespace url_shortener::tui
{

/**
 * @brief Runs the interactive `tui` mode to completion and returns a process
 * exit code.
 *
 * Steps, in order (spec subtask 19 "Behaviour of run_tui"):
 * 1. Refuse unless both stdin and stdout are terminals (one stderr line,
 *    exit 2) - the TUI cannot drive a pipe or a redirected stream.
 * 2. Map @p cli to @ref TuiOptions via @ref to_tui_options; an invalid
 *    `--theme` reports one stderr line and exits 2 (defense in depth - the
 *    parser already rejects it).
 * 3. Initialize logging to a `File` sink **before** building the runtime
 *    (@ref tui_logging_config, K8/D14/T-119): never a terminal sink while the
 *    TUI owns the alt-screen. The file is created `0600` by the hardened File
 *    sink; a File-open failure reports one stderr line and exits 2.
 * 4. Build the one command runtime from @p backend - the **active**
 *    `LinkBackendHandle` `main` installed - via
 *    `composition::build_command_runtime`, so the TUI reads and writes the one
 *    installed store, never a second one (contracts C8/C11, threat model T-114,
 *    the serve-stdio split-store bug fixed in ba2d150). The local operator opts
 *    explicitly into file paths (`allow_paths = true`, T-95 default-deny
 * lifted) so `link.import`/`link.export` `path` works. Constructs no registry,
 * job runner or service bundle of its own. A wiring failure exits 4.
 * 5. Wrap the runtime's registry in an @ref InProcessClient as the
 *    `"local-operator"` principal - `ControlSet::Admin`, or `User` under
 *    `--read-only` - and run @ref App, which restores the terminal on every
 * exit path (RAII + SIGTERM, threat model T-123). On quit, drain and join the
 *    process-wide job runtime before returning.
 *
 * @param cli The parsed launch options (read-only, --yes, theme, log file).
 * @param config Server configuration the built runtime borrows by reference
 * (base domain, slug length, `max_request_body_bytes` for the inline cap); must
 * outlive the call.
 * @param backend The process-wide storage handle `main` installed; its
 * `repository()` selects the browse lister so `link.list` reads the same
 * backend `link.create`/`update`/`delete` write through. Must outlive the call.
 * @return `0` on a clean quit; `2` on a non-terminal refusal, an invalid theme,
 * or a log-file open failure; `4` on a runtime-build failure or an escaping
 * exception (matches `cli::ExitCodeForAppError` grouping).
 */
int run_tui(const TuiCliOptions& cli,
            const ServerConfig& config,
            const app::LinkBackendHandle& backend);

/**
 * @brief Maps the parsed CLI launch options to the TUI's own @ref TuiOptions,
 * validating the theme spelling.
 *
 * The one place that turns the string theme into a @ref ThemeName (via
 * `parse_theme_name`) and copies `read_only`/`assume_yes` across;
 * @ref TuiOptions::max_inline_bytes is taken from
 * `ServerConfig::max_request_body_bytes` (the inline import/paste DoS bound,
 * threat model T-121).
 *
 * @param cli The parsed launch options.
 * @param config Server configuration supplying `max_request_body_bytes`.
 * @param error When non-null and the mapping fails (an unrecognized
 * `cli.theme`), receives a fixed, value-free diagnostic; left untouched on
 * success.
 * @return The mapped options, or `std::nullopt` when `cli.theme` is not one of
 * `dark|light|mono` (in which case @p error is written when non-null).
 */
std::optional<TuiOptions> to_tui_options(const TuiCliOptions& cli,
                                         const ServerConfig& config,
                                         std::string* error);

/**
 * @brief Derives the logging configuration for `tui` mode (decision log D14).
 *
 * Always a `File` sink, never `Stdout`/`Stderr` and never disabled, so every
 * mutation's audit line is kept durably off the screen while the TUI owns the
 * terminal: at `cli.log_file` when given, otherwise at `url_shortener-tui.log`
 * in the current directory (append; the File sink creates it `0600`).
 *
 * @param cli The parsed launch options; `log_file` selects the path.
 * @return A `LoggingConfig` with `enabled == true`, `sink == LogSink::File` and
 * a non-empty `file_path`.
 */
observability::LoggingConfig tui_logging_config(const TuiCliOptions& cli);

/**
 * @brief Reports whether the process stdio can drive the interactive TUI: both
 * stdin and stdout must be attached to a terminal.
 *
 * Exposed so `main()` can refuse a non-interactive `tui` invocation (a pipe or
 * redirect on either stream) *before* the process-wide command/job runtime is
 * constructed, so the refusal emits only its single stderr line and never the
 * job runner's teardown log (BUG-3, 06.0/20 smoke). @ref run_tui performs the
 * same check as defense in depth for any direct caller.
 *
 * @return `true` iff both `stdin` and `stdout` are interactive terminals.
 */
bool tui_stdio_is_interactive();

}  // namespace url_shortener::tui
