#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>
#include <url_shortener/core/config.h>

namespace url_shortener {

/**
 * @brief HTTP/HTTPS server lifecycle facade.
 */
class HttpServer
{
public:
    HttpServer(boost::asio::io_context& io_context, ServerConfig config);

    void run();
    void stop();
    void reloadTlsContext();

private:
    boost::asio::ssl::context buildTlsContext() const;

    boost::asio::io_context& io_context_;  ///< Shared event loop.
    ServerConfig config_;  ///< Runtime configuration snapshot.

    std::unique_ptr<boost::asio::ip::tcp::acceptor> http_acceptor_;  ///< HTTP acceptor.
    std::unique_ptr<boost::asio::ip::tcp::acceptor> https_acceptor_;  ///< HTTPS acceptor.
    std::shared_ptr<boost::asio::ssl::context> tls_context_;  ///< Shared TLS context.

    std::atomic<uint64_t> tls_handshake_success_count_{0};  ///< Successful TLS handshakes.
    std::atomic<uint64_t> tls_handshake_failure_count_{0};  ///< Failed TLS handshakes.
};

/**
 * @brief Parse textual TLS client-auth mode into enum value.
 *
 * @param value Mode string (`none`, `optional`, `required`).
 * @return TlsConfig::ClientAuthMode Parsed mode (defaults to `none`).
 */
TlsConfig::ClientAuthMode parseClientAuthMode(const std::string& value);

} // namespace url_shortener
