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
#include <url_shortener/cli/cli_parser.h>
#include <url_shortener/observability/LoggerFactory.h>
#include <url_shortener/url_shortener.h>
#include <url_shortener/uri_map_singleton.h>

namespace fs = std::filesystem;
namespace net = boost::asio;
constexpr std::string_view uri_filename = "uri.txt";

/**
 * @brief Process entry point: parse configuration, boot server, and handle shutdown signals.
 *
 * @param argc Number of CLI arguments.
 * @param argv CLI argument vector.
 * @return int Process exit code (0 on success).
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

        auto& config = parsed.config;
        url_shortener::observability::LoggerFactory::Initialize({});
        auto logger = url_shortener::observability::LoggerFactory::Component("main");
        logger.info("startup_begin");
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
