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
    /**
     * @brief Client certificate verification policy for inbound TLS sessions.
     */
    enum class ClientAuthMode
    {
        /** Do not request a client certificate. */
        none,
        /** Request a client certificate but allow connections without one. */
        optional,
        /** Require a valid client certificate for every TLS connection. */
        required
    };

    /** Enable HTTPS listener and TLS negotiation when true. */
    bool enabled = false;
    /** Port used by the HTTPS listener. */
    uint16_t port = 443;
    /** Path to PEM certificate chain presented by the server. */
    std::string certificate_chain_file;
    /** Path to PEM private key for the certificate chain. */
    std::string private_key_file;
    /** Optional private-key decryption passphrase. */
    std::optional<std::string> private_key_passphrase;
    /** Optional CA bundle file used for client certificate validation. */
    std::optional<std::string> ca_file;
    /** Optional CA bundle directory used for client certificate validation. */
    std::optional<std::string> ca_path;
    /** Minimum accepted TLS protocol version (e.g. TLS1.2, TLS1.3). */
    std::string min_version = "TLS1.2";
    /** TLS 1.3 cipher suites, expressed in OpenSSL cipher-suite syntax. */
    std::string cipher_suites =
        "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256";
    /** TLS 1.2-and-earlier cipher list, expressed in OpenSSL syntax. */
    std::string ciphers =
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256";
    /** Elliptic curves allowed for ECDHE key exchange. */
    std::string curves = "X25519:P-256";
    /** ALPN protocol preference list sent by the server. */
    std::string alpn = "http/1.1";
    /** Enable stateless TLS session tickets when true. */
    bool session_tickets = false;
    /** Enable OpenSSL session cache for TLS resumptions when true. */
    bool session_cache = true;
    /** Enable OCSP stapling support when true (if runtime supports it). */
    bool ocsp_stapling = false;
    /** Client certificate verification mode for inbound connections. */
    ClientAuthMode client_auth_mode = ClientAuthMode::none;
};

/**
 * @brief Runtime configuration for HTTP/HTTPS listeners and request constraints.
 */
struct ServerConfig
{
    /** Port used by the plaintext HTTP listener. */
    uint16_t http_port = 8000;
    /** Enable plaintext HTTP listener when true. */
    bool http_enabled = true;
    /** Redirect HTTP traffic to HTTPS when both listeners are enabled. */
    bool http_redirect_to_https = false;
    /** Optional HSTS max-age value in seconds for HTTPS responses. */
    std::optional<uint32_t> hsts_max_age;
    /** Exit startup immediately when TLS configuration is invalid. */
    bool fail_fast_on_invalid_tls_config = true;
    /** Nested TLS listener and cryptographic settings. */
    TlsConfig tls;
    /** Base URL used by short-link generation responses. */
    std::string shortener_base_domain = "http://localhost:8000";
    /** Default redirect style for generated links (temporary/permanent). */
    std::string shortener_default_redirect_type = "temporary";
    /** Length of randomly generated short-link slugs. */
    uint32_t shortener_generated_slug_length = 7;
    /** Allow redirects to private-network targets when true. */
    bool shortener_allow_private_targets = false;
    /** Enable analytics event recording when true. */
    bool analytics_enabled = true;
    /** Capacity of the in-memory analytics queue. */
    uint32_t analytics_queue_capacity = 1024;
    /** Salt used when hashing analytics client identifiers. */
    std::string analytics_client_hash_salt = "dev-analytics-salt";
    /** Upper bound for inbound request ID header length. */
    uint32_t request_id_max_length = 64;
    /** Maximum accepted request body size in bytes. */
    uint32_t max_request_body_bytes = 65536;
    /** Maximum accepted request target length in bytes. */
    uint32_t max_request_target_length = 2048;
};

/**
 * @brief Asynchronous HTTP/HTTPS server entry point.
 */
class HttpServer
{
public:
    /**
     * @brief Construct a server bound to an io_context and immutable runtime config.
     * @param io_context Event loop used for async networking operations.
     * @param config Runtime configuration copied into the server instance.
     */
    HttpServer(boost::asio::io_context& io_context, ServerConfig config);

    /** Start listeners and accept incoming connections. */
    void run();
    /** Stop listeners and terminate active server processing. */
    void stop();
    /** Rebuild and replace TLS context using the current configuration. */
    void reloadTlsContext();

private:
    /**
     * @brief Build a configured OpenSSL context from @ref config_.
     * @return Fully initialized TLS context ready for server usage.
     */
    boost::asio::ssl::context buildTlsContext() const;

    boost::asio::io_context& io_context_;
    ServerConfig config_;

    std::unique_ptr<boost::asio::ip::tcp::acceptor> http_acceptor_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> https_acceptor_;
    std::shared_ptr<boost::asio::ssl::context> tls_context_;

    std::atomic<uint64_t> tls_handshake_success_count_{0};
    std::atomic<uint64_t> tls_handshake_failure_count_{0};
};

/**
 * @brief Parse CLI/client-auth text value into a @ref TlsConfig::ClientAuthMode.
 * @param value Mode text (none, optional, or required).
 * @return Parsed client-auth mode enum.
 */
TlsConfig::ClientAuthMode parseClientAuthMode(const std::string& value);
