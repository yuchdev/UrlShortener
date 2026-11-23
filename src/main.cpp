/**
 * @file main.cpp
 * @brief Application entry point for url_shortener service.
 */
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <boost/asio.hpp>
#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/cmd_dispatch.hpp>
#include <url_shortener/cli/link_command_dispatch.hpp>
#include <url_shortener/cli/serve_stdio_dispatch.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/job_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/uri_map_singleton.hpp>
#include <url_shortener/url_shortener.hpp>

#if URLSHORTENER_WITH_TUI
#    include <url_shortener/tui/tui_entry.hpp>
#endif

using namespace url_shortener;

namespace fs = std::filesystem;
namespace net = boost::asio;
constexpr std::string_view uri_filename = "uri.txt";

/**
 * @brief Process entry point: parse configuration, boot the storage backend,
 * run the CLI or the server, and handle shutdown signals.
 *
 * Startup order (server mode): parse args -> `--help` -> InitLinkBackend
 * (installs the one store) -> install the process-wide command runtime (built
 * from the active handle) -> LinkBackend::mark_serving() -> logging ->
 * io_context + HttpServer -> uri.txt -> server.run() -> io_context.run().
 * Startup order (CLI mode): parse args -> `--help` -> InitLinkBackend ->
 * install the process-wide command runtime -> LinkBackend::mark_serving() ->
 * DispatchLinkCommand -> backend.flush(). InitLinkBackend therefore runs before
 * the runtime is built and before HttpServer/DispatchLinkCommand, so redirect
 * handlers, REST handlers and the CLI all see the same installed store and the
 * one command registry over it (plan.md C11).
 *
 * Shutdown order (every exit path, driven by guard destruction order): the
 * `runtime_cleanup` guard resets the process-wide command runtime first (it
 * borrows the store/repository), then the `backend_cleanup` guard runs
 * LinkBackend::shutdown() - stopping the installed store's background flusher
 * and resetting the holder on this thread while the logger is still alive - and
 * only then does the `backend` handle destruct, releasing the store before its
 * adapters and clock. A `RepositoryLinkStore` is thus never flushed or
 * destroyed during static destruction (carried requirement 5).
 *
 * @param argc Number of CLI arguments.
 * @param argv CLI argument vector.
 * @return int Process exit code (0 on success; 2 on a CLI usage error - a
 * malformed invocation in any mode, reported concisely on stderr with no FATAL
 * log line; 4 on a storage bootstrap failure; 1 on any other unhandled
 * exception).
 */
int main(int argc, char* argv[])
{
    try {
        CliParser cli;
        ParseResult parsed;
        try {
            parsed = cli.parse(argc, argv);
        } catch (const std::exception& e) {
            // Exception boundary for a malformed CLI invocation (user ruling):
            // a usage error must never reach the FATAL `unhandled_exception`
            // handler below. CliParser classifies every command-line usage
            // error (unknown subcommand, unknown `link` verb, unknown flag, a
            // flag missing its value, a bad flag value, a mutually-exclusive
            // flag combination, ...) as a `UsageError`, whose message is
            // already control-sanitized and value-free (never a supplied flag
            // value; threat model T-77 / CWE-117), so echoing what() here can
            // neither leak a secret nor forge a log line.
            //
            // `cmd` keeps its established envelope-style boundary: any parse
            // failure in `cmd` mode (a `UsageError` or, e.g., a `std::runtime_
            // error` from a bad environment variable) is reported as the
            // documented `cmd failed: invalid_request: <reason>` line and exits
            // 2 (docs/commands/README.md). Malformed `cmd` command NAMES and
            // `--arg` operands never reach here at all — the parser carries
            // them through to the dispatcher's `invalid_envelope` envelope.
            //
            // Every OTHER mode (server, `link <verb>`, `serve-stdio`, `tui`)
            // reports a `UsageError` as a concise `error: <message>` line plus
            // a
            // `--help` hint on stderr and exits 2 (bad input), not 1. Any
            // non-usage exception (e.g. the env-var `std::runtime_error`, or a
            // genuinely unexpected failure) is rethrown to the FATAL path below
            // (exit 1), so server-mode startup failures keep today's behavior.
            if (argc >= 2 && std::string(argv[1]) == "cmd") {
                std::cerr << "cmd failed: invalid_request: " << e.what()
                          << '\n';
                return 2;
            }
            if (dynamic_cast<const UsageError*>(&e) != nullptr) {
                std::cerr << "error: " << e.what() << '\n'
                          << "Try 'url_shortener --help' for usage.\n";
                return 2;
            }
            throw;
        }

        if (parsed.help_requested) {
            std::cout << parsed.help_text;
            return 0;
        }

        // Storage backend, installed at the one point correct for every mode:
        // after `--help`/flag parsing, before the CLI dispatch and before any
        // HttpServer is constructed (subtask 02.0/12). Without --storage-config
        // (and env) the handle stays inactive and installs nothing, so the path
        // is byte-for-byte today's in-memory default (plan.md C3/C4). `backend`
        // is declared before io_context/server so it is destroyed after them.
        url_shortener::app::LinkBackendHandle backend;
        // RAII cleanup: guarantees LinkBackend::shutdown() runs once on every
        // exit path (normal return, error return, or exception during
        // unwinding) before `backend` destructs. Declared after `backend` so it
        // destructs first (carried requirement 2; see the shutdown order
        // above).
        struct link_backend_cleanup
        {
            ~link_backend_cleanup()
            {
                url_shortener::app::LinkBackend::shutdown();
            }
        } backend_cleanup;
        // RAII cleanup for the process-wide command runtime. Declared AFTER
        // backend_cleanup so it destructs FIRST: the runtime's link service
        // references LinkBackend::store() and its lister holds the active
        // metadata repository, so drop the runtime
        // (reset_process_command_runtime) before LinkBackend::shutdown()
        // retires the store and before `backend` destructs its adapters. A
        // no-op when nothing was installed.
        struct process_runtime_cleanup
        {
            ~process_runtime_cleanup()
            {
                url_shortener::composition::reset_process_command_runtime();
            }
        } runtime_cleanup;
        try {
            backend = url_shortener::app::InitLinkBackend(parsed.config);
        } catch (const std::runtime_error& e) {
            // e.what() is a fixed, value-free message: never a DSN, a path, or
            // raw yaml-cpp output (sanitized in InitLinkBackend, subtask 11).
            std::cerr << e.what() << '\n';
            return url_shortener::cli::ExitCodeForAppError(
                url_shortener::app::AppErrorCode::storage_failure);
        } catch (const std::exception&) {
            // Any other bootstrap failure (e.g. std::logic_error from a
            // malformed redis_address port) still maps to the storage exit
            // code; its what() is not known to be value-free, so print a
            // fixed message instead.
            std::cerr << "storage backend unavailable\n";
            return url_shortener::cli::ExitCodeForAppError(
                url_shortener::app::AppErrorCode::storage_failure);
        }

#if URLSHORTENER_WITH_TUI
        // The interactive `tui` mode needs a real terminal. Refuse a
        // pipe/redirect here — BEFORE the process-wide command/job runtime is
        // built below — so a non-interactive invocation (`url_shortener tui <
        // /dev/null`) emits only its single stderr refusal line and never the
        // job runner's `job_runner_shutdown` teardown log, which would
        // otherwise reach the still-default stderr log sink (BUG-3, 06.0/20
        // smoke). run_tui repeats this check as defense in depth.
        if (parsed.tui.has_value()
            && !url_shortener::tui::tui_stdio_is_interactive())
        {
            std::cerr << "tui: refusing to start without an interactive "
                         "terminal\n";
            return 2;
        }
#endif

        // Build and publish the one process-wide command runtime from the
        // ACTIVE backend handle, before mark_serving() (install asserts
        // !serving) and before any CLI dispatch or HttpServer can serve a
        // command. Every transport (REST now, CLI `cmd` in subtask 13) then
        // shares this one registry over the one installed store, so link.list /
        // db.rows read exactly what link CRUD writes — no split brain under
        // --storage-config. `parsed.config` outlives the runtime:
        // runtime_cleanup resets it on every exit path before `parsed`
        // destructs.
        //
        // File-path capability (threat model T-95, default-deny): the
        // server/REST runtime stays deny, so POST /api/v1/commands refuses a
        // `path`. The local-operator one-shot CLI modes (`cmd`, `link <verb>`)
        // opt in so `link.import`/`link.export` may name a local file; this
        // install serves those dispatchers via acquire_command_runtime.
        // Invariant: a one-shot invocation (`link <verb>`, `cmd <name>` or
        // `serve-stdio`) short-circuits below and never constructs an
        // HttpServer.
        const bool one_shot_mode = parsed.command.has_value()
            || parsed.cmd.has_value() || parsed.serve_stdio.has_value();

        // File-path capability on the INSTALLED runtime (threat model T-95,
        // default-deny): only the one-shot CLI modes that execute file-naming
        // commands THROUGH the installed runtime opt in — `link <verb>` and
        // `cmd` reach `link.import`/`link.export` via acquire_command_runtime.
        // The server/REST runtime stays deny (POST /api/v1/commands refuses a
        // `path`). `serve-stdio` builds its OWN runtime with its own file-path
        // policy (05.0/17) and never uses this installed one, so it is
        // deliberately excluded here; allow_paths == true still implies the
        // process never builds HttpServer.
        const bool installed_runtime_file_ops =
            parsed.command.has_value() || parsed.cmd.has_value();
        url_shortener::composition::CommandRuntimeOptions runtime_options;
        if (installed_runtime_file_ops) {
            runtime_options.file_paths.allow_paths = true;
        }
        url_shortener::composition::install_process_command_runtime(
            std::make_shared<url_shortener::composition::CommandRuntime>(
                url_shortener::composition::build_command_runtime(
                    parsed.config, backend, runtime_options)));

        // Latch "serving" before any request-handling thread can exist (before
        // CLI dispatch and before HttpServer is constructed), so install()/
        // reset() can no longer race a request thread holding store()'s
        // reference (carried requirement 1; threat model T-39).
        url_shortener::app::LinkBackend::mark_serving();

        // One-shot CLI mode: a recognized `link <verb>` short-circuits here,
        // before any io_context/HttpServer is constructed and before uri.txt is
        // loaded, so CLI mode never starts the server (plan.md C4/C5).
        if (parsed.command.has_value()) {
            const int rc = url_shortener::cli::DispatchLinkCommand(
                *parsed.command, parsed.config);
            backend.flush();  // shutdown-only; no-op on the default path.
            return rc;
        }

        // One-shot CLI mode: a recognized `cmd <name>` short-circuits here the
        // same way, running through the shared command registry as the local
        // operator and never constructing an HttpServer (subtask 13; C4/C5).
        if (parsed.cmd.has_value()) {
            const int rc = url_shortener::cli::DispatchCmdCommand(
                *parsed.cmd, parsed.config);
            backend.flush();  // shutdown-only; no-op on the default path.
            return rc;
        }

        // One-shot stdio transport: `serve-stdio` runs the NDJSON stream over
        // stdin/stdout and short-circuits here, before any
        // io_context/HttpServer is constructed and before uri.txt is loaded, so
        // it never starts the server (plan.md C5; subtask 17).
        if (parsed.serve_stdio.has_value()) {
            // Pass the ACTIVE backend handle so serve-stdio's runtime lister
            // reads the configured repository `main` installed (no split store
            // under --storage-config; 05.0/17).
            const int rc = url_shortener::cli::RunServeStdio(
                parsed.config, *parsed.serve_stdio, backend);
            backend.flush();  // shutdown-only; no-op on the default path.
            return rc;
        }

        // tui-mode-begin
        // One-shot interactive terminal UI: `tui` short-circuits here, after
        // the backend bootstrap and before any network listener or data file is
        // touched, so it never starts the server (plan.md C5). run_tui builds
        // the one command runtime from the ACTIVE backend handle (plan.md C11,
        // threat model T-114) - the registry is never built here. With the TUI
        // compiled out the word still parses and fails with a clear message.
        if (parsed.tui.has_value()) {
#if URLSHORTENER_WITH_TUI
            const int rc = url_shortener::tui::run_tui(
                *parsed.tui, parsed.config, backend);
            backend.flush();  // shutdown-only; no-op on the default path.
            return rc;
#else
            std::cerr << "built without TUI support "
                         "(URLSHORTENER_ENABLE_TUI=OFF)\n";
            return 2;
#endif
        }
        // tui-mode-end

        auto& config = parsed.config;
        // Fail closed if logging cannot be initialized: never start the server
        // with an audit/log sink that silently discards or misroutes output.
        // The diagnostic is value-free (it never contains a configured path).
        std::string log_init_error;
        if (!url_shortener::observability::LoggerFactory::Initialize(
                {}, &log_init_error))
        {
            std::cerr << "fatal: logging initialization failed: "
                      << log_init_error << '\n';
            return 1;
        }
        auto logger =
            url_shortener::observability::LoggerFactory::Component("main");
        logger.info("startup_begin");
        if (backend.active()) {
            logger.info("storage_backend", {{"summary", backend.summary()}});
        }
        // Non-secret visibility of the opt-in command API gate (subtask 08/09):
        // never logs peers, tokens or DSNs. When enabled with role admin, warn
        // that admin is granted to loopback peers only - and that behind a
        // same-host reverse proxy every request is loopback, so such
        // deployments should keep role user (threat model T-64/T-65).
        {
            const std::vector<url_shortener::observability::LogField> fields = {
                {"enabled", config.command_api_enabled ? "true" : "false"},
                {"role", config.command_api_role}};
            if (config.command_api_enabled
                && config.command_api_role == "admin")
            {
                logger.warn(
                    "command_api: admin role grants admin to loopback "
                    "peers only; behind a same-host reverse proxy every "
                    "request is loopback, so keep role user there",
                    fields);
            }
            else {
                logger.info("command_api", fields);
            }
        }
        net::io_context io_context;
        HttpServer server(io_context, config);

        if (fs::exists(uri_filename)) {
            UriMapSingleton::getInstance().deserialize(
                std::string(uri_filename));
            logger.info("data_loaded", {{"path", std::string(uri_filename)}});
        }
        else {
            logger.info("data_file_missing",
                        {{"path", std::string(uri_filename)}});
        }

        server.run();

#if defined(__unix__) || defined(__APPLE__)
        net::signal_set signals(io_context, SIGINT, SIGTERM, SIGHUP);
#else
        net::signal_set signals(io_context, SIGINT, SIGTERM);
#endif
        auto signal_handler = std::make_shared<
            std::function<void(const boost::system::error_code&, int)>>();
        *signal_handler =
            [&](const boost::system::error_code& ec, int signal_number)
        {
            if (ec) {
                return;
            }
#if defined(__unix__) || defined(__APPLE__)
            if (signal_number == SIGHUP) {
                try {
                    server.reloadTlsContext();
                } catch (const std::exception& ex) {
                    logger.error("tls_reload_failed", {{"error", ex.what()}});
                }
                signals.async_wait(*signal_handler);
                return;
            }
#endif
            server.stop();
            UriMapSingleton::getInstance().serialize(std::string(uri_filename));
            // Drain (bounded), cancel and join the process-wide job runner
            // before flushing the backend, so an in-flight import/export job
            // finishes or is cancelled while its service dependencies are still
            // alive (05.0/20; order per 02.0/12: stop -> drain jobs -> flush).
            url_shortener::composition::shutdown_process_job_runtime(
                std::chrono::seconds(30));
            backend.flush();  // persist pending redirect counters before stop.
            io_context.stop();
        };
        signals.async_wait(*signal_handler);

        io_context.run();
    } catch (const std::exception& e) {
        auto logger =
            url_shortener::observability::LoggerFactory::Component("main");
        logger.fatal("unhandled_exception", {{"error", e.what()}});
        return 1;
    }

    return 0;
}
