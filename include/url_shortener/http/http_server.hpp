#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl/context.hpp>
#include <url_shortener/core/config.hpp>

namespace url_shortener
{

/**
 * @brief HTTP/HTTPS server lifecycle facade that owns the TCP acceptors
 * and TLS context for the application.
 *
 * Lifetime: does not own `io_context`; the caller must keep it alive for
 * at least as long as this object (including across `stop()`).  `config`
 * is moved in and owned; no external lifetime constraint on the original.
 *
 * Thread-safety: `run()`, `stop()`, and `reloadTlsContext()` must be
 * called from the same thread that drives `io_context_`.  The TLS
 * handshake counters are updated from async callbacks and are atomic.
 */
class HttpServer
{
public:
    /**
     * @brief Stores the `io_context` reference and moves `config` in;
     * does not start listening.  Normalizes and validates
     * `config.shortener_base_domain` before storing the config.
     * @param io_context Asio event loop that drives all async operations;
     *        must outlive this object.
     * @param config     Runtime configuration snapshot; moved in and owned.
     */
    HttpServer(boost::asio::io_context& io_context, ServerConfig config);

    /**
     * @brief Starts the HTTP and/or HTTPS acceptor loops according to
     * `config_.http_enabled` and `config_.tls.enabled`.
     *
     * Does not call `io_context_.run()`.  The caller (typically `main`)
     * is responsible for running the io_context after `run()` returns.
     * Each accepted connection spawns a `PlainSession` or `TlsSession`.
     */
    void run();

    /**
     * @brief Cancels and closes the HTTP and HTTPS acceptors, stopping
     * acceptance of new connections.
     *
     * Does not call `io_context_.stop()` or join any threads.  In-flight
     * sessions continue to completion through the io_context.  Logs the
     * lifetime TLS handshake success/failure counts before returning.
     */
    void stop();

    /**
     * @brief Rebuilds the TLS context from the current certificate and
     * key files and replaces `tls_context_` atomically.
     *
     * New TLS sessions created after this call use the new context.
     * In-flight TLS sessions are unaffected because they hold their own
     * `shared_ptr<ssl::context>`.  A no-op if `config_.tls.enabled` is
     * `false`.  Increments `tls_reload_success_total` on success and
     * `tls_reload_failure_total` on failure.
     * @throws Any exception thrown by `buildTlsContext()` (e.g.
     *         `std::runtime_error` for missing/mismatched cert/key files
     *         or SSL library errors); the reload failure counter is
     *         incremented before rethrowing.
     */
    void reloadTlsContext();

private:
    /**
     * @brief Builds and validates a new `ssl::context` from the current
     * `config_.tls` settings.
     * @throws `std::runtime_error` if cert or key files are missing,
     *         the private key doesn't match the certificate, the key file
     *         is too permissive (group/world-readable on POSIX), or the
     *         SSL library rejects the cipher/curve configuration.
     * @return A fully initialized `ssl::context` ready for use.
     */
    boost::asio::ssl::context buildTlsContext() const;

    ///< Asio event loop driving all async operations; not owned,
    ///< must outlive this object.
    boost::asio::io_context& io_context_;

    ///< Runtime configuration; moved in at construction and owned.
    ServerConfig config_;

    ///< TCP acceptor for plain HTTP; null when HTTP is disabled.
    std::unique_ptr<boost::asio::ip::tcp::acceptor> http_acceptor_;

    ///< TCP acceptor for HTTPS; null when TLS is disabled.
    std::unique_ptr<boost::asio::ip::tcp::acceptor> https_acceptor_;

    ///< TLS context shared with all active TLS sessions; replaced
    ///< atomically by `reloadTlsContext()`.  Existing sessions hold
    ///< their own copy of this shared_ptr and are unaffected by reload.
    std::shared_ptr<boost::asio::ssl::context> tls_context_;

    ///< Lifetime count of successful TLS handshakes; updated from
    ///< async handshake callbacks and logged in `stop()`.
    std::atomic<uint64_t> tls_handshake_success_count_ {0};

    ///< Lifetime count of failed TLS handshakes; updated from async
    ///< handshake callbacks and logged in `stop()`.
    std::atomic<uint64_t> tls_handshake_failure_count_ {0};
};

/**
 * @brief Parse textual TLS client-auth mode into enum value.
 *
 * @param value Mode string (`none`, `optional`, `required`).
 * @return TlsConfig::ClientAuthMode Parsed mode (defaults to `none`).
 */
TlsConfig::ClientAuthMode parseClientAuthMode(const std::string& value);

}  // namespace url_shortener
