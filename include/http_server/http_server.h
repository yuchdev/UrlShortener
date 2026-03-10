#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>

struct TlsConfig
{
    enum class ClientAuthMode
    {
        none,
        optional,
        required
    };

    bool enabled = false;
    uint16_t port = 443;
    std::string certificate_chain_file;
    std::string private_key_file;
    std::optional<std::string> private_key_passphrase;
    std::optional<std::string> ca_file;
    std::optional<std::string> ca_path;
    std::string min_version = "TLS1.2";
    std::string cipher_suites =
        "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256";
    std::string ciphers =
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256";
    std::string curves = "X25519:P-256";
    std::string alpn = "http/1.1";
    bool session_tickets = false;
    bool session_cache = true;
    bool ocsp_stapling = false;
    ClientAuthMode client_auth_mode = ClientAuthMode::none;
};

struct ServerConfig
{
    uint16_t http_port = 8000;
    bool http_enabled = true;
    bool http_redirect_to_https = false;
    std::optional<uint32_t> hsts_max_age;
    bool fail_fast_on_invalid_tls_config = true;
    TlsConfig tls;
    std::string shortener_base_domain = "http://localhost:8000";
    std::string shortener_default_redirect_type = "temporary";
    uint32_t shortener_generated_slug_length = 7;
    bool shortener_allow_private_targets = false;
};

class HttpServer
{
public:
    HttpServer(boost::asio::io_context& io_context, ServerConfig config);

    void run();
    void stop();
    void reloadTlsContext();

private:
    boost::asio::ssl::context buildTlsContext() const;

    boost::asio::io_context& io_context_;
    ServerConfig config_;

    std::unique_ptr<boost::asio::ip::tcp::acceptor> http_acceptor_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> https_acceptor_;
    std::shared_ptr<boost::asio::ssl::context> tls_context_;

    std::atomic<uint64_t> tls_handshake_success_count_{0};
    std::atomic<uint64_t> tls_handshake_failure_count_{0};
};

TlsConfig::ClientAuthMode parseClientAuthMode(const std::string& value);
