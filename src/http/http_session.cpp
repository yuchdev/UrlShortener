#include <url_shortener/http/http_session.h>
#include <url_shortener/http/http_server.h>
#include <url_shortener/http/request_handlers.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/core/config.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <chrono>

namespace url_shortener {

namespace {
constexpr auto request_timeout = std::chrono::seconds(30);
constexpr std::string_view internal_request_id_header = "X-Internal-Request-Id";
}

// PlainSession implementation

PlainSession::PlainSession(tcp::socket socket, const ServerConfig& config)
    : socket_(std::move(socket))
    , config_(config)
{
}

void PlainSession::run()
{
    socket_.expires_after(request_timeout);
    http::async_read(socket_, buffer_, req_,
        beast::bind_front_handler(&PlainSession::onRead, shared_from_this()));
}

void PlainSession::onRead(const beast::error_code& ec, std::size_t)
{
    if (ec) {
        metricsRegistry().parse_errors_total.fetch_add(1);
        return;
    }

    const RequestContext ctx{
        resolveRequestId(req_, config_),
        routeLabelFor(std::string(req_.target())),
        std::chrono::steady_clock::now() };
    req_.set(internal_request_id_header, ctx.request_id);
    metricsRegistry().inflight_requests.fetch_add(1);

    if (req_.target().size() > config_.max_request_target_length) {
        metricsRegistry().malformed_requests_total.fetch_add(1);
        res_ = makeApiErrorResponse(req_, config_, false, 414, "target_too_large", "request target too large");
    }
    else if (req_.body().size() > config_.max_request_body_bytes) {
        metricsRegistry().malformed_requests_total.fetch_add(1);
        res_ = makeApiErrorResponse(req_, config_, false, 413, "payload_too_large", "request body too large");
    }
    else if (config_.http_redirect_to_https && config_.tls.enabled) {
        res_ = makeRedirectResponse(req_, config_);
    }
    else {
        res_ = handleShortenerRequest(req_, config_, false);
    }

    metricsRegistry().inflight_requests.fetch_sub(1);
    recordHttpMetric(req_, ctx.route_label, res_.result_int());
    const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ctx.started_at).count();
    logStructured("INFO", "request completed",
        {
            {"component", "url_shortener"},
            {"request_id", ctx.request_id},
            {"method", std::string(req_.method_string())},
            {"path", std::string(req_.target())},
            {"route", ctx.route_label},
            {"status", std::to_string(res_.result_int())},
            {"response_size", std::to_string(res_.payload_size().value_or(0))},
            {"latency_ms", std::to_string(latency_ms)}
        });

    http::async_write(socket_, res_, beast::bind_front_handler(&PlainSession::onWrite, shared_from_this()));
}

void PlainSession::onWrite(const beast::error_code&, std::size_t)
{
    beast::error_code ignored;
    socket_.socket().shutdown(tcp::socket::shutdown_send, ignored);
}

// TlsSession implementation

TlsSession::TlsSession(tcp::socket socket,
    std::shared_ptr<ssl::context> context,
    const ServerConfig& config,
    std::atomic<uint64_t>& success_counter,
    std::atomic<uint64_t>& failure_counter)
    : stream_(std::move(socket), *context)
    , config_(config)
    , success_counter_(success_counter)
    , failure_counter_(failure_counter)
{
}

void TlsSession::run()
{
    beast::get_lowest_layer(stream_).expires_after(request_timeout);
    stream_.async_handshake(ssl::stream_base::server,
        beast::bind_front_handler(&TlsSession::onHandshake, shared_from_this()));
}

void TlsSession::onHandshake(const beast::error_code& ec)
{
    if (ec) {
        ++failure_counter_;
        logStructured("ERROR", "tls_handshake_failed", {{"component","tls"},{"error", ec.message()}});
        return;
    }

    ++success_counter_;
    auto* ssl_handle = stream_.native_handle();
    logStructured("INFO", "tls_handshake_success", {{"component","tls"},{"version",SSL_get_version(ssl_handle)},{"cipher",SSL_get_cipher_name(ssl_handle)}});

    beast::get_lowest_layer(stream_).expires_after(request_timeout);
    http::async_read(stream_, buffer_, req_,
        beast::bind_front_handler(&TlsSession::onRead, shared_from_this()));
}

void TlsSession::onRead(const beast::error_code& ec, std::size_t)
{
    if (ec) {
        metricsRegistry().parse_errors_total.fetch_add(1);
        return;
    }

    const RequestContext ctx{
        resolveRequestId(req_, config_),
        routeLabelFor(std::string(req_.target())),
        std::chrono::steady_clock::now() };
    req_.set(internal_request_id_header, ctx.request_id);
    metricsRegistry().inflight_requests.fetch_add(1);

    if (req_.target().size() > config_.max_request_target_length) {
        metricsRegistry().malformed_requests_total.fetch_add(1);
        res_ = makeApiErrorResponse(req_, config_, true, 414, "target_too_large", "request target too large");
    }
    else if (req_.body().size() > config_.max_request_body_bytes) {
        metricsRegistry().malformed_requests_total.fetch_add(1);
        res_ = makeApiErrorResponse(req_, config_, true, 413, "payload_too_large", "request body too large");
    }
    else {
        res_ = handleShortenerRequest(req_, config_, true);
    }

    metricsRegistry().inflight_requests.fetch_sub(1);
    recordHttpMetric(req_, ctx.route_label, res_.result_int());
    const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - ctx.started_at).count();
    logStructured("INFO", "request completed",
        {
            {"component", "url_shortener"},
            {"request_id", ctx.request_id},
            {"method", std::string(req_.method_string())},
            {"path", std::string(req_.target())},
            {"route", ctx.route_label},
            {"status", std::to_string(res_.result_int())},
            {"response_size", std::to_string(res_.payload_size().value_or(0))},
            {"latency_ms", std::to_string(latency_ms)}
        });

    http::async_write(stream_, res_,
        beast::bind_front_handler(&TlsSession::onWrite, shared_from_this()));
}

void TlsSession::onWrite(const beast::error_code&, std::size_t)
{
    beast::get_lowest_layer(stream_).expires_after(request_timeout);
    stream_.async_shutdown(
        beast::bind_front_handler(&TlsSession::onShutdown, shared_from_this()));
}

void TlsSession::onShutdown(const beast::error_code&)
{
    beast::get_lowest_layer(stream_).close();
}

} // namespace url_shortener
