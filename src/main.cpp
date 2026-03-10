#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

#include <boost/asio.hpp>
#include <http_server/http_server.h>
#include <http_server/uri_map_singleton.h>

namespace fs = std::filesystem;
namespace net = boost::asio;
constexpr std::string_view uri_filename = "uri.txt";

namespace {

bool parseBool(const std::string& value)
{
    return value == "1" || value == "true" || value == "on" || value == "yes";
}

uint16_t parsePort(const std::string& value)
{
    const int port = std::stoi(value);
    if (port <= 0 || port > 65535) {
        throw std::out_of_range("Invalid port number");
    }
    return static_cast<uint16_t>(port);
}

ServerConfig parseArgs(int argc, char* argv[])
{
    ServerConfig config;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--http-port" && i + 1 < argc) {
            config.http_port = parsePort(argv[++i]);
        }
        else if (arg == "--http-enabled" && i + 1 < argc) {
            config.http_enabled = parseBool(argv[++i]);
        }
        else if (arg == "--http-redirect-to-https" && i + 1 < argc) {
            config.http_redirect_to_https = parseBool(argv[++i]);
        }
        else if (arg == "--hsts-max-age" && i + 1 < argc) {
            config.hsts_max_age = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (arg == "--tls-enabled" && i + 1 < argc) {
            config.tls.enabled = parseBool(argv[++i]);
        }
        else if (arg == "--https-port" && i + 1 < argc) {
            config.tls.port = parsePort(argv[++i]);
        }
        else if (arg == "--tls-cert" && i + 1 < argc) {
            config.tls.certificate_chain_file = argv[++i];
        }
        else if (arg == "--tls-key" && i + 1 < argc) {
            config.tls.private_key_file = argv[++i];
        }
        else if (arg == "--tls-key-passphrase" && i + 1 < argc) {
            config.tls.private_key_passphrase = std::string(argv[++i]);
        }
        else if (arg == "--tls-ca-file" && i + 1 < argc) {
            config.tls.ca_file = std::string(argv[++i]);
        }
        else if (arg == "--tls-ca-path" && i + 1 < argc) {
            config.tls.ca_path = std::string(argv[++i]);
        }
        else if (arg == "--tls-min-version" && i + 1 < argc) {
            config.tls.min_version = argv[++i];
        }
        else if (arg == "--tls-cipher-suites" && i + 1 < argc) {
            config.tls.cipher_suites = argv[++i];
        }
        else if (arg == "--tls-ciphers" && i + 1 < argc) {
            config.tls.ciphers = argv[++i];
        }
        else if (arg == "--tls-curves" && i + 1 < argc) {
            config.tls.curves = argv[++i];
        }
        else if (arg == "--tls-alpn" && i + 1 < argc) {
            config.tls.alpn = argv[++i];
        }
        else if (arg == "--tls-session-tickets" && i + 1 < argc) {
            config.tls.session_tickets = parseBool(argv[++i]);
        }
        else if (arg == "--tls-session-cache" && i + 1 < argc) {
            config.tls.session_cache = parseBool(argv[++i]);
        }
        else if (arg == "--tls-client-auth" && i + 1 < argc) {
            config.tls.client_auth_mode = parseClientAuthMode(argv[++i]);
        }
        else if (arg[0] != '-') {
            config.http_port = parsePort(arg);
        }
        else {
            throw std::invalid_argument("Unknown or incomplete argument: " + arg);
        }
    }

    return config;
}

} // namespace

int main(int argc, char* argv[])
{
    try {
        auto config = parseArgs(argc, argv);
        net::io_context io_context;
        HttpServer server(io_context, config);

        if (fs::exists(uri_filename)) {
            UriMapSingleton::getInstance().deserialize(std::string(uri_filename));
            std::cout << "Data loaded from " << uri_filename << '\n';
        }
        else {
            std::cout << "No existing data file found" << '\n';
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
                    std::cerr << "TLS reload failed: " << ex.what() << '\n';
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
        std::cerr << "Exception: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
