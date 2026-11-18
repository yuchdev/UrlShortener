/**
 * @file tui_entry.cpp
 * @brief Process wiring for the interactive `tui` mode (Task 06.0 subtask 19):
 * logging init -> build the one command runtime over the active backend ->
 * in-process client -> run the App -> drain the job runtime.
 *
 * IMPORTANT (milestone contracts C5/C8/C11, threat model T-114): this
 * translation unit must never start the network server, construct a Boost.Asio
 * execution context, bind a socket, build its own command registry / job runner
 * / service bundle, or touch the legacy redirect data file or its map
 * singleton. It includes nothing under `url_shortener/http/` and no
 * `boost/asio` headers (the subtask 19 boundary test asserts this by scanning
 * the file). The one registry it runs on comes from
 * `composition::build_command_runtime` over the **active**
 * `app::LinkBackendHandle` `main` installed, so the TUI reads and writes the
 * one store the rest of the process uses - never a second, empty one.
 */
#include <chrono>
#include <cstdio>
#include <exception>
#include <iostream>
#include <optional>
#include <string>

#include <url_shortener/tui/tui_entry.hpp>

#if defined(_WIN32)
#    include <io.h>
#else
#    include <unistd.h>
#endif

#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/commands/command_descriptor.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/job_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/observability/logging_config.hpp>
#include <url_shortener/security/control_set.hpp>
#include <url_shortener/tui/app.hpp>
#include <url_shortener/tui/in_process_client.hpp>
#include <url_shortener/tui/screens/dashboard_screen.hpp>
#include <url_shortener/tui/screens/import_export_screen.hpp>
#include <url_shortener/tui/screens/jobs_screen.hpp>
#include <url_shortener/tui/screens/links_screen.hpp>
#include <url_shortener/tui/screens/palette_screen.hpp>
#include <url_shortener/tui/screens/settings_screen.hpp>
#include <url_shortener/tui/screens/tables_screen.hpp>
#include <url_shortener/tui/theme.hpp>

namespace url_shortener::tui
{
namespace
{

/// Final bounded join of the process-wide runner on quit: running imports are
/// cancelled cooperatively and partial results kept (ADR 0002 section 4).
constexpr std::chrono::seconds k_shutdown_timeout {5};

/// Reports whether standard input is attached to an interactive terminal.
bool stdin_is_tty()
{
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdin)) != 0;
#else
    return ::isatty(STDIN_FILENO) != 0;
#endif
}

/// Reports whether standard output is attached to an interactive terminal.
bool stdout_is_tty()
{
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

}  // namespace

observability::LoggingConfig tui_logging_config(const TuiCliOptions& cli)
{
    observability::LoggingConfig cfg;
    // Decision log D14 / threat model T-119: always a File sink, never a
    // terminal sink (that would corrupt the alt-screen) and never disabled (the
    // audit trail of every mutation must land somewhere durable).
    cfg.enabled = true;
    cfg.sink = observability::LogSink::File;
    cfg.file_path = (cli.log_file.has_value() && !cli.log_file->empty())
        ? *cli.log_file
        : std::string("url_shortener-tui.log");
    return cfg;
}

std::optional<TuiOptions> to_tui_options(const TuiCliOptions& cli,
                                         const ServerConfig& config,
                                         std::string* error)
{
    const std::optional<ThemeName> theme = parse_theme_name(cli.theme);
    if (!theme.has_value()) {
        if (error != nullptr) {
            *error = "invalid theme (expected dark, light, or mono)";
        }
        return std::nullopt;
    }

    TuiOptions options;
    options.theme = *theme;
    options.read_only = cli.read_only;
    options.assume_yes = cli.assume_yes;
    // The inline import/paste DoS bound (threat model T-121) tracks the request
    // body limit, so every transport shares one inline cap.
    options.max_inline_bytes = config.max_request_body_bytes;
    return options;
}

int run_tui(const TuiCliOptions& cli,
            const ServerConfig& config,
            const app::LinkBackendHandle& backend)
{
    // Step 1: the TUI needs a real terminal; refuse a pipe/redirect cleanly so
    // a non-interactive caller fails fast instead of leaving a half-opened
    // screen.
    if (!stdin_is_tty() || !stdout_is_tty()) {
        std::cerr << "tui: refusing to start without an interactive terminal\n";
        return 2;
    }

    // Step 2: map options and validate the theme (the parser already rejects an
    // invalid theme; this is defense in depth and the single mapping point).
    std::string option_error;
    const std::optional<TuiOptions> options =
        to_tui_options(cli, config, &option_error);
    if (!options.has_value()) {
        std::cerr << "tui: " << option_error << '\n';
        return 2;
    }

    // Step 3: initialize logging to a File sink BEFORE building the runtime
    // (K8/D14). An uninitialized logger defaults to an enabled stderr sink,
    // whose audit lines would corrupt the alt-screen; the File sink creates the
    // file 0600 with O_NOFOLLOW/O_CLOEXEC (threat model T-119). Fail closed: if
    // the file cannot be opened, refuse to start (value-free diagnostic).
    std::string log_error;
    if (!observability::LoggerFactory::Initialize(tui_logging_config(cli),
                                                  &log_error))
    {
        std::cerr << "tui: cannot open log file: " << log_error << '\n';
        return 2;
    }

    // Step 4: the one composition root (contract C11). The runtime is built
    // over
    // @p backend - the same ACTIVE handle `main` installed - so the browse
    // lister reads the configured repository that link CRUD writes through; a
    // stale/empty store under --storage-config (threat model T-114, the
    // serve-stdio split-store bug) is thereby avoided. As a local-operator mode
    // the TUI opts explicitly into file paths (T-95 default-deny is lifted
    // here) so link.import/link.export `path` works. No designated initializers
    // (C++17): assign the members.
    //
    // Known limitation (06.0/19 security follow-up, T-118): unlike serve-stdio,
    // the TUI exposes no `--no-file-paths`/`--path-root` flag and sets no
    // confinement root here, so the capability is the operator's full
    // filesystem. --read-only still blocks link.import writes server-side (the
    // registry denies the write permission), but link.export can read any file
    // the operator can read. Acceptable for a local-operator tool; a
    // confinement flag is a documented follow-up, not added here.
    composition::CommandRuntimeOptions runtime_options;
    runtime_options.file_paths.allow_paths = true;

    composition::CommandRuntime runtime;
    try {
        runtime = composition::build_command_runtime(
            config, backend, runtime_options);
    } catch (const std::exception&) {
        // An unrecoverable wiring failure (reserved for programming/startup
        // errors by the repo rule). Value-free diagnostic; storage/bootstrap
        // exit code (4). Logging is a File sink, so nothing reaches the screen.
        std::cerr << "tui: failed to build command runtime\n";
        return 4;
    }

    // Step 5: run the session as the local operator. The caller role is the
    // sole authorization input (contract C7): Admin by default, User under
    // --read-only; the registry still enforces every permission.
    commands::CallerContext caller;
    caller.principal = "local-operator";
    caller.control_set = cli.read_only ? ControlSet::User : ControlSet::Admin;
    caller.transport = "tui";
    InProcessClient client(*runtime.registry, caller);

    // Step 6: run the App. It restores the terminal on every exit path (RAII
    // guard + SIGTERM handler, threat model T-123). Registering the real
    // screens here is the one place that names every screen factory;
    // not-yet-built tabs keep their placeholders inside App::run.
    int rc = 0;
    try {
        App app(client, *options);
        // The Dashboard tab (D2) aggregates read commands into the landing
        // view; it is handed the live catalog here (a screen never sees
        // ICommandClient itself, contract C8) so it can feature-detect a
        // stats.summary command. Replaces the Dashboard tab's placeholder.
        app.register_screen(
            [catalog = client.commands()](ScreenContext& screen_context)
            { return make_dashboard_screen(screen_context, catalog); });
        app.register_screen(make_settings_screen);
        // The Links tab (D3) drives the generic link.* calls through a bespoke
        // table; it is handed the live catalog here (a screen never sees
        // ICommandClient itself, contract C8) so it can resolve the CommandInfo
        // each mutation needs. Replaces the Links tab's placeholder.
        app.register_screen(
            [catalog = client.commands()](ScreenContext& screen_context)
            { return make_links_screen(screen_context, catalog); });
        // The Tables tab (D6) browses the db.* logical views through a two-pane
        // table; it is handed the live catalog here (a screen never sees
        // ICommandClient itself, contract C8) so it can resolve the link.create
        // CommandInfo its insert form needs. Replaces the Tables tab's
        // placeholder.
        app.register_screen(
            [catalog = client.commands()](ScreenContext& screen_context)
            { return make_tables_screen(screen_context, catalog); });
        // The Command Palette lists the live catalog; it is passed in here (a
        // screen never sees ICommandClient itself, contract C8) so a command
        // added to the registry appears with a working form and zero TUI code
        // (D9). Replaces the palette tab's placeholder.
        app.register_screen(
            [catalog = client.commands()](ScreenContext& screen_context)
            { return make_palette_screen(screen_context, catalog); });
        // The Jobs tab (D8) polls job.list and cancels through run_mutation; it
        // is handed the live catalog so it can recover the job.cancel
        // permission for the read-only/confirm gate (contract C8). Replaces the
        // Jobs tab's placeholder.
        app.register_screen(
            [catalog = client.commands()](ScreenContext& screen_context)
            { return make_jobs_screen(screen_context, catalog); });
        // The Import/Export tab (D7) submits link.import / link.export as jobs
        // and hands the accepted job to the Jobs screen; it is handed the live
        // catalog here (a screen never sees ICommandClient itself, contract C8)
        // so it can resolve those two CommandInfos for the confirm / read-only
        // gate. Replaces the Import/Export tab's placeholder.
        app.register_screen(
            [catalog = client.commands()](ScreenContext& screen_context)
            { return make_import_export_screen(screen_context, catalog); });
        rc = app.run();
    } catch (const std::exception& e) {
        // App's RAII guard has already restored the terminal; record the
        // failure to the File sink (off-screen) and report a bootstrap/internal
        // exit.
        observability::LoggerFactory::Component("tui").error(
            "tui_failed", {{"error", e.what()}});
        rc = 4;
    }

    // Step 7: drain, cancel and join the process-wide job runtime before
    // returning, so a running import/export job finishes or is cancelled
    // cooperatively while its dependencies are still alive (05.0/20).
    composition::shutdown_process_job_runtime(k_shutdown_timeout);
    return rc;
}

}  // namespace url_shortener::tui
