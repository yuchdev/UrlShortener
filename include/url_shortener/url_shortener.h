/**
 * @file url_shortener.h
 * @brief Public server configuration and HTTP server interface declarations.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>

/**
 * @brief TLS runtime configuration.
 */
struct TlsConfig
{
    /**
     * @brief Client certificate verification mode.
     */
    enum class ClientAuthMode
    {
        none,
        optional,
        required
    };

    bool enabled = false;  ///< Enables TLS listener when true.
    uint16_t port = 443;  ///< HTTPS listening port.
    std::string certificate_chain_file;  ///< PEM certificate chain path.
    std::string private_key_file;  ///< PEM private key path.
    std::optional<std::string> private_key_passphrase;  ///< Optional private key passphrase.
    std::optional<std::string> ca_file;  ///< Optional CA bundle file path.
    std::optional<std::string> ca_path;  ///< Optional CA directory path.
    std::string min_version = "TLS1.2";  ///< Minimum allowed TLS protocol version.
    std::string cipher_suites =
        "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256";
    std::string ciphers =
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256";
    std::string curves = "X25519:P-256";  ///< Allowed ECDHE curves/groups.
    std::string alpn = "http/1.1";  ///< ALPN protocol list.
    bool session_tickets = false;  ///< Enables TLS session tickets.
    bool session_cache = true;  ///< Enables TLS session cache.
    bool ocsp_stapling = false;  ///< Enables OCSP stapling when supported.
    ClientAuthMode client_auth_mode = ClientAuthMode::none;  ///< Client certificate auth mode.
};

/**
 * @brief Top-level server configuration consumed by HttpServer.
 */
struct ServerConfig
{
    uint16_t http_port = 8000;  ///< HTTP listening port.
    bool http_enabled = true;  ///< Enables plaintext HTTP listener.
    bool http_redirect_to_https = false;  ///< Redirect HTTP traffic to HTTPS when enabled.
    std::optional<uint32_t> hsts_max_age;  ///< Optional HSTS max-age value in seconds.
    bool fail_fast_on_invalid_tls_config = true;  ///< Fail startup on invalid TLS config.
    TlsConfig tls;  ///< Nested TLS configuration block.
    std::string shortener_base_domain = "http://localhost:8000";  ///< Base domain for short URL rendering.
    std::string shortener_default_redirect_type = "temporary";  ///< Default redirect type for new links.
    std::optional<uint32_t> shortener_default_expiry_seconds;  ///< Optional default expiry for new links.
    uint32_t shortener_generated_slug_length = 7;  ///< Generated slug length.
    bool shortener_allow_private_targets = false;  ///< Allow private/intranet destination URLs.
    bool analytics_enabled = true;  ///< Enable click analytics collection.
    uint32_t analytics_queue_capacity = 1024;  ///< In-memory analytics queue capacity.
    std::string analytics_client_hash_salt = "dev-analytics-salt";  ///< HMAC salt for analytics client IDs.
    uint32_t request_id_max_length = 64;  ///< Maximum accepted request id length.
    uint32_t max_request_body_bytes = 65536;  ///< Maximum request payload bytes.
    uint32_t max_request_target_length = 2048;  ///< Maximum request target/path length.
};

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
