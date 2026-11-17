/**
 * @file main.cpp
 * @brief Application entry point for url_shortener service.
 */
#include <filesystem>
#include <functional>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <string_view>

#include <boost/asio.hpp>
#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/cmd_dispatch.hpp>
#include <url_shortener/cli/link_command_dispatch.hpp>
#include <url_shortener/composition/command_runtime.hpp>
#include <url_shortener/composition/link_backend_bootstrap.hpp>
#include <url_shortener/observability/logger_factory.hpp>
#include <url_shortener/url_shortener.hpp>
#include <url_shortener/uri_map_singleton.hpp>

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
 * Startup order (CLI mode): parse args -> `--help` -> InitLinkBackend -> install
 * the process-wide command runtime -> LinkBackend::mark_serving() ->
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
 * adapters and clock. A `RepositoryLinkStore` is thus never flushed or destroyed
 * during static destruction (carried requirement 5).
 *
 * @param argc Number of CLI arguments.
 * @param argv CLI argument vector.
 * @return int Process exit code (0 on success; 4 on a storage bootstrap
 * failure; 1 on any other unhandled exception).
 */
int main(int argc, char* argv[])
{
    try {
        CliParser cli;
        ParseResult parsed = cli.parse(argc, argv);

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
        // destructs first (carried requirement 2; see the shutdown order above).
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
        // metadata repository, so drop the runtime (reset_process_command_runtime)
        // before LinkBackend::shutdown() retires the store and before `backend`
        // destructs its adapters. A no-op when nothing was installed.
        struct process_runtime_cleanup
        {
            ~process_runtime_cleanup()
            {
                url_shortener::composition::reset_process_command_runtime();
            }
        } runtime_cleanup;
        try {
            backend = url_shortener::app::InitLinkBackend(parsed.config);
        }
        catch (const std::runtime_error& e) {
            // e.what() is a fixed, value-free message: never a DSN, a path, or
            // raw yaml-cpp output (sanitized in InitLinkBackend, subtask 11).
            std::cerr << e.what() << '\n';
            return url_shortener::cli::ExitCodeForAppError(
                url_shortener::app::AppErrorCode::storage_failure);
        }
        catch (const std::exception&) {
            // Any other bootstrap failure (e.g. std::logic_error from a
            // malformed redis_address port) still maps to the storage exit
            // code; its what() is not known to be value-free, so print a
            // fixed message instead.
            std::cerr << "storage backend unavailable\n";
            return url_shortener::cli::ExitCodeForAppError(
                url_shortener::app::AppErrorCode::storage_failure);
        }

        // Build and publish the one process-wide command runtime from the
        // ACTIVE backend handle, before mark_serving() (install asserts
        // !serving) and before any CLI dispatch or HttpServer can serve a
        // command. Every transport (REST now, CLI `cmd` in subtask 13) then
        // shares this one registry over the one installed store, so link.list /
        // db.rows read exactly what link CRUD writes — no split brain under
        // --storage-config. `parsed.config` outlives the runtime: runtime_cleanup
        // resets it on every exit path before `parsed` destructs.
        url_shortener::composition::install_process_command_runtime(
            std::make_shared<url_shortener::composition::CommandRuntime>(
                url_shortener::composition::build_command_runtime(
                    parsed.config, backend)));

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

        auto& config = parsed.config;
        // Fail closed if logging cannot be initialized: never start the server
        // with an audit/log sink that silently discards or misroutes output.
        // The diagnostic is value-free (it never contains a configured path).
        std::string log_init_error;
        if (!url_shortener::observability::LoggerFactory::Initialize(
                {}, &log_init_error)) {
            std::cerr << "fatal: logging initialization failed: "
                      << log_init_error << '\n';
            return 1;
        }
        auto logger = url_shortener::observability::LoggerFactory::Component("main");
        logger.info("startup_begin");
        if (backend.active()) {
            logger.info("storage_backend", {{"summary", backend.summary()}});
        }
        // Non-secret visibility of the opt-in command API gate (subtask 08/09):
        // never logs peers, tokens or DSNs. When enabled with role admin, warn
        // that admin is granted to loopback peers only - and that behind a
        // same-host reverse proxy every request is loopback, so such deployments
        // should keep role user (threat model T-64/T-65).
        {
            const std::vector<url_shortener::observability::LogField> fields = {
                {"enabled", config.command_api_enabled ? "true" : "false"},
                {"role", config.command_api_role}};
            if (config.command_api_enabled
                && config.command_api_role == "admin") {
                logger.warn("command_api: admin role grants admin to loopback "
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
            UriMapSingleton::getInstance().deserialize(std::string(uri_filename));
            logger.info("data_loaded", {{"path", std::string(uri_filename)}});
        }
        else {
            logger.info("data_file_missing", {{"path", std::string(uri_filename)}});
        }

        server.run();

#if defined(__unix__) || defined(__APPLE__)
        net::signal_set signals(io_context, SIGINT, SIGTERM, SIGHUP);
#else
        net::signal_set signals(io_context, SIGINT, SIGTERM);
#endif
        auto signal_handler = std::make_shared<std::function<void(const boost::system::error_code&, int)>>();
        *signal_handler = [&](const boost::system::error_code& ec, int signal_number) {
            if (ec) {
                return;
            }
#if defined(__unix__) || defined(__APPLE__)
            if (signal_number == SIGHUP) {
                try {
                    server.reloadTlsContext();
                }
                catch (const std::exception& ex) {
                    logger.error("tls_reload_failed", {{"error", ex.what()}});
                }
                signals.async_wait(*signal_handler);
                return;
            }
#endif
            server.stop();
            UriMapSingleton::getInstance().serialize(std::string(uri_filename));
            backend.flush();  // persist pending redirect counters before stop.
            io_context.stop();
        };
        signals.async_wait(*signal_handler);

        io_context.run();
    }
    catch (const std::exception& e) {
        auto logger = url_shortener::observability::LoggerFactory::Component("main"); logger.fatal("unhandled_exception", {{"error", e.what()}});
        return 1;
    }

    return 0;
}
