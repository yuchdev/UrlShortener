#pragma once

#include <atomic>
#include <memory>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

struct ServerConfig;

namespace url_shortener
{

/**
 * @brief Async HTTP (plain-text) session handling a single request-
 * response exchange over a TCP connection.
 *
 * Derives from `enable_shared_from_this` so the session extends its own
 * lifetime through Boost.Asio's async machinery: calling `run()` starts
 * the async read/write chain; the session is kept alive by shared_ptr
 * copies captured in each completion handler until the final `onWrite`
 * closes the socket.
 *
 * Lifetime: does not own `config`; the caller (`HttpServer`) must keep
 * it alive for the duration of all sessions it creates.
 */
class PlainSession : public std::enable_shared_from_this<PlainSession>
{
public:
    /**
     * @brief Takes ownership of `socket` and stores a reference to
     * `config`.
     * @param socket Accepted TCP socket; moved in and owned.
     * @param config Server-wide runtime configuration; must outlive
     *        this session object (and all its async continuations).
     */
    PlainSession(boost::asio::ip::tcp::socket socket,
                 const ServerConfig& config);

    /**
     * @brief Starts the async read chain with a 30-second deadline.
     * Must be called once after construction, typically as
     * `std::make_shared<PlainSession>(...)->run()`.
     */
    void run();

private:
    /**
     * @brief Boost.Asio completion handler called when the HTTP request
     * has been fully read.  Applies request-size limits, dispatches to
     * `handleShortenerRequest` (or issues an HTTPS redirect if
     * `config_.http_redirect_to_https` is set), records metrics, logs
     * the completed request, then starts an async write.
     */
    void onRead(const boost::beast::error_code& ec, std::size_t);

    /**
     * @brief Boost.Asio completion handler called when the response has
     * been fully written.  Performs a graceful TCP shutdown (send side).
     */
    void onWrite(const boost::beast::error_code&, std::size_t);

    ///< Owned TCP stream; wraps the accepted socket.
    boost::beast::tcp_stream socket_;

    ///< Per-request read buffer; reused for the single request this
    ///< session handles.
    boost::beast::flat_buffer buffer_;

    ///< Parsed HTTP request; populated by the async_read in `run()`.
    boost::beast::http::request<boost::beast::http::string_body> req_;

    ///< Response built by the handler; kept alive until `onWrite` fires.
    boost::beast::http::response<boost::beast::http::string_body> res_;

    ///< Not owned; must outlive this session and all its async
    ///< continuations.
    const ServerConfig& config_;
};

/**
 * @brief Async HTTPS (TLS) session handling a single request-response
 * exchange over a TLS-wrapped TCP connection.
 *
 * Derives from `enable_shared_from_this`; the session's lifetime is
 * extended through TLS handshake → async_read → async_write →
 * async_shutdown chain via shared_ptr copies captured in each
 * completion handler.
 *
 * Lifetime: does not own `config`, `success_counter`, or
 * `failure_counter`; all three must outlive this session and all its
 * async continuations.  The `ssl::context` referenced by `stream_` is
 * kept alive through OpenSSL's internal reference counting for the
 * duration of the session; the `context` shared_ptr is used only during
 * construction.
 */
class TlsSession : public std::enable_shared_from_this<TlsSession>
{
public:
    /**
     * @brief Takes ownership of `socket`, initialises `stream_` from
     * `*context`, and stores references to `config` and the handshake
     * counters.
     * @param socket          Accepted TCP socket; moved in and owned.
     * @param context         TLS context used to initialize the SSL
     *        stream; not stored as a member — the underlying `ssl::context`
     *        is kept alive by OpenSSL's internal reference counting.
     * @param config          Server-wide runtime configuration; must
     *        outlive this session and all its async continuations.
     * @param success_counter Atomic counter to increment on a successful
     *        TLS handshake; must outlive this session.
     * @param failure_counter Atomic counter to increment on a failed TLS
     *        handshake; must outlive this session.
     */
    TlsSession(boost::asio::ip::tcp::socket socket,
               std::shared_ptr<boost::asio::ssl::context> context,
               const ServerConfig& config,
               std::atomic<uint64_t>& success_counter,
               std::atomic<uint64_t>& failure_counter);

    /**
     * @brief Starts the TLS handshake with a 30-second deadline.
     * Must be called once after construction.
     */
    void run();

private:
    /**
     * @brief Boost.Asio completion handler called when the TLS handshake
     * completes.  Increments the appropriate counter; on success starts
     * an async_read; on failure logs and returns.
     */
    void onHandshake(const boost::beast::error_code& ec);

    /**
     * @brief Boost.Asio completion handler called when the HTTP request
     * has been fully read over TLS.  Applies request-size limits,
     * dispatches to `handleShortenerRequest`, records metrics, logs, then
     * starts an async write.
     */
    void onRead(const boost::beast::error_code& ec, std::size_t);

    /**
     * @brief Boost.Asio completion handler called when the response has
     * been fully written.  Starts the async TLS shutdown sequence.
     */
    void onWrite(const boost::beast::error_code&, std::size_t);

    /**
     * @brief Boost.Asio completion handler called when the TLS shutdown
     * exchange completes.  Closes the underlying TCP layer.
     */
    void onShutdown(const boost::beast::error_code&);

    ///< TLS stream owning the SSL state; wraps the accepted socket.
    boost::asio::ssl::stream<boost::beast::tcp_stream> stream_;

    ///< Per-request read buffer.
    boost::beast::flat_buffer buffer_;

    ///< Parsed HTTP request; populated by the async_read in `run()`.
    boost::beast::http::request<boost::beast::http::string_body> req_;

    ///< Response built by the handler; kept alive until `onWrite` fires.
    boost::beast::http::response<boost::beast::http::string_body> res_;

    ///< Not owned; must outlive this session and all its async
    ///< continuations.
    const ServerConfig& config_;

    ///< Not owned; incremented in `onHandshake` on success.
    std::atomic<uint64_t>& success_counter_;

    ///< Not owned; incremented in `onHandshake` on failure.
    std::atomic<uint64_t>& failure_counter_;
};

}  // namespace url_shortener
