/**
 * @file main.cpp
 * @brief Application entry point for url_shortener service.
 */
#include <filesystem>
#include <functional>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/asio.hpp>
#include <url_shortener/app/app_error.hpp>
#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/cli/cli_parser.hpp>
#include <url_shortener/cli/link_command_dispatch.hpp>
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
 * (installs the one store) -> LinkBackend::mark_serving() -> logging ->
 * io_context + HttpServer -> uri.txt -> server.run() -> io_context.run().
 * Startup order (CLI mode): parse args -> `--help` -> InitLinkBackend ->
 * LinkBackend::mark_serving() -> DispatchLinkCommand -> backend.flush().
 * InitLinkBackend therefore runs before HttpServer is constructed and before
 * DispatchLinkCommand, so redirect handlers, REST handlers and the CLI all see
 * the same installed store (plan.md C11).
 *
 * Shutdown order (every exit path): LinkBackend::shutdown() runs first (via the
 * `backend_cleanup` guard, or during stack unwinding on an exception) - it
 * stops the installed store's background flusher and resets the holder on this
 * thread while the logger is still alive - and only then does the `backend`
 * handle destruct, releasing the store before its adapters and clock. A
 * `RepositoryLinkStore` is thus never flushed or destroyed during static
 * destruction (carried requirement 5).
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

        auto& config = parsed.config;
        url_shortener::observability::LoggerFactory::Initialize({});
        auto logger = url_shortener::observability::LoggerFactory::Component("main");
        logger.info("startup_begin");
        if (backend.active()) {
            logger.info("storage_backend", {{"summary", backend.summary()}});
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
