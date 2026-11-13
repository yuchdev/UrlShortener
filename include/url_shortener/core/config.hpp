#pragma once

#include <cstdint>
#include <optional>
#include <string>

/**
 * @brief TLS runtime configuration consumed by `HttpServer` when
 * constructing the `ssl::context` for the HTTPS listener.
 */
struct TlsConfig
{
    /**
     * @brief Client certificate verification mode for mTLS.
     */
    enum class ClientAuthMode
    {
        ///< No client certificate is requested or verified.
        none,

        ///< A client certificate is requested but not required;
        ///< connections without a certificate are accepted.
        optional,

        ///< A valid client certificate is required; the connection
        ///< is rejected if the client does not present one.
        required
    };

    ///< Enables the TLS listener when `true`.
    bool enabled = false;

    ///< HTTPS listening port.
    uint16_t port = 443;

    ///< Filesystem path to the PEM certificate chain.
    std::string certificate_chain_file;

    ///< Filesystem path to the PEM private key. Never log the key
    ///< contents.
    std::string private_key_file;

    ///< Optional passphrase for an encrypted private key; absent
    ///< means the key is unencrypted. Treat as a secret.
    std::optional<std::string> private_key_passphrase;

    ///< Optional CA bundle file used to verify client certificates.
    std::optional<std::string> ca_file;

    ///< Optional CA directory used to verify client certificates.
    std::optional<std::string> ca_path;

    ///< Minimum allowed TLS protocol version.
    std::string min_version = "TLS1.2";

    ///< Colon-separated TLS 1.3 cipher suite names passed to
    ///< OpenSSL's `SSL_CTX_set_ciphersuites`.
    std::string cipher_suites =
        "TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_"
        "SHA256";

    ///< Colon-separated TLS 1.2 cipher string passed to
    ///< OpenSSL's `SSL_CTX_set_cipher_list`.
    std::string ciphers =
        "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
        "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:"
        "ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256";

    ///< Colon-separated ECDHE curves/groups passed to OpenSSL.
    std::string curves = "X25519:P-256";

    ///< ALPN protocol list advertised to clients.
    std::string alpn = "http/1.1";

    ///< Enables TLS session tickets; `false` by default to preserve
    ///< forward secrecy.
    bool session_tickets = false;

    ///< Enables the server-side TLS session cache.
    bool session_cache = true;

    ///< Enables OCSP stapling when supported by the OpenSSL build.
    bool ocsp_stapling = false;

    ///< Client certificate verification mode (mTLS).
    ClientAuthMode client_auth_mode = ClientAuthMode::none;
};

/**
 * @brief Top-level server runtime configuration, parsed from CLI
 * flags by `src/cli_parser.cpp` and injected into `HttpServer`,
 * `LinkService`, and the application layer.
 */
struct ServerConfig
{
    ///< Plaintext HTTP listening port.
    uint16_t http_port = 8000;

    ///< Enables the plaintext HTTP listener.
    bool http_enabled = true;

    ///< Redirects HTTP traffic to HTTPS when both listeners are active.
    bool http_redirect_to_https = false;

    ///< Optional HSTS `max-age` directive in seconds; absent
    ///< disables the HSTS header.
    std::optional<uint32_t> hsts_max_age;

    ///< Aborts startup on an invalid TLS configuration instead of
    ///< silently downgrading.
    bool fail_fast_on_invalid_tls_config = true;

    ///< Nested TLS configuration block.
    TlsConfig tls;

    ///< Base URL prepended to slugs when constructing `short_url`
    ///< fields (e.g. `"https://short.example.com"`). Must be an
    ///< absolute HTTP/HTTPS URL with no path; validated and
    ///< normalized by `normalizeAndValidateBaseDomain` at startup.
    std::string shortener_base_domain = "http://localhost:8000";

    ///< Default redirect type applied when a link is created
    ///< without an explicit type; accepted values: `"temporary"`
    ///< (HTTP 302) and `"permanent"` (HTTP 301).
    std::string shortener_default_redirect_type = "temporary";

    ///< Default link lifetime in seconds applied at creation time
    ///< when the request omits `expires_at`; absent means links
    ///< do not expire by default.
    std::optional<uint32_t> shortener_default_expiry_seconds;

    ///< Length in characters of randomly generated slugs.
    uint32_t shortener_generated_slug_length = 7;

    ///< When `false`, target URLs resolving to private/loopback
    ///< hosts are rejected (SSRF guard).
    bool shortener_allow_private_targets = false;

    ///< Enables click-event analytics collection.
    bool analytics_enabled = true;

    ///< Capacity of the in-memory `BoundedClickEventQueue` in
    ///< events; excess events are silently dropped
    ///< (`EnqueueResult::DroppedFull`) rather than blocking the
    ///< redirect path.
    uint32_t analytics_queue_capacity = 1024;

    ///< HMAC-SHA256 key used to hash raw client identifiers before
    ///< analytics storage. Treat as a secret — never log this
    ///< value verbatim (use
    ///< `observability::redactSecretValue` before logging).
    std::string analytics_client_hash_salt = "dev-analytics-salt";

    ///< Maximum accepted `X-Request-Id` header value length in bytes.
    uint32_t request_id_max_length = 64;

    ///< Maximum request body size in bytes; larger requests are rejected.
    uint32_t max_request_body_bytes = 65536;

    ///< Maximum request-target (path + query) length in bytes.
    uint32_t max_request_target_length = 2048;
};
