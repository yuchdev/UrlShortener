#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <map>
#include <boost/beast/http.hpp>
#include <boost/asio/ssl/context.hpp>
#include <url_shortener/core/types.h>
#include <url_shortener/core/config.h>

namespace url_shortener {

namespace beast = boost::beast;
namespace http = beast::http;
namespace ssl = boost::asio::ssl;

struct RequestContext
{
    std::string request_id;  ///< Request correlation identifier.
    std::string route_label;  ///< Normalized route label for metrics/logging.
    std::chrono::steady_clock::time_point started_at;  ///< Request start timestamp.
};

struct MetricsRegistry
{
    std::atomic<uint64_t> inflight_requests{0};  ///< Current inflight HTTP requests.
    std::atomic<uint64_t> parse_errors_total{0};  ///< HTTP parser error count.
    std::atomic<uint64_t> malformed_requests_total{0};  ///< Malformed request count.
    std::atomic<uint64_t> tls_reload_success_total{0};  ///< Successful TLS reloads.
    std::atomic<uint64_t> tls_reload_failure_total{0};  ///< Failed TLS reloads.

    std::mutex mutex;  ///< Protects http_requests_total map.
    std::map<std::string, uint64_t> http_requests_total;  ///< Per-route/status request counters.
};

MetricsRegistry& metricsRegistry();

std::string renderMetrics();

http::response<http::string_body> makeResponse(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned int status,
    const std::string& body,
    const std::string& content_type);

http::response<http::string_body> makeApiErrorResponse(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned status,
    const std::string& code,
    const std::string& message);

http::response<http::string_body> makeRedirectResponse(
    const http::request<http::string_body>& req,
    const ServerConfig& config);

http::response<http::string_body> handleShortenerRequest(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls);

http::response<http::string_body> handleApplicationRequest(
    const http::request<http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls);

// Helpers for sessions and server
std::string resolveRequestId(const http::request<http::string_body>& req, const ServerConfig& config);
std::string requestIdFromRequest(const http::request<http::string_body>& req);
std::string routeLabelFor(const std::string& target);
void recordHttpMetric(const http::request<http::string_body>& req, const std::string& route_label, const unsigned status);
void logStructured(const std::string& level, const std::string& msg, const std::map<std::string, std::string>& fields);
void configureTlsVersion(boost::asio::ssl::context& context, const std::string& min_version);
uint64_t certExpiryDaysRemaining(const std::string& cert_path);
void validatePrivateKeyFilePermissions(const std::string& key_file);

} // namespace url_shortener
