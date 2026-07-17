#pragma once

#include <string>
#include <chrono>
#include <atomic>
#include <mutex>
#include <map>
#include <functional>
#include <boost/beast/http.hpp>
#include <boost/asio/ssl/context.hpp>
#include <url_shortener/core/types.h>
#include <url_shortener/core/config.h>
#include <url_shortener/core/utils.h>

namespace url_shortener {

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

boost::beast::http::response<boost::beast::http::string_body> makeResponse(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned int status,
    const std::string& body,
    const std::string& content_type);

boost::beast::http::response<boost::beast::http::string_body> makeApiErrorResponse(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned status,
    const std::string& code,
    const std::string& message);

boost::beast::http::response<boost::beast::http::string_body> makeRedirectResponse(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config);

boost::beast::http::response<boost::beast::http::string_body> handleShortenerRequest(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls);

/// Implements the legacy URI-store fallback. Router fallback handlers wrap this
/// function so the storage behavior remains centralized during route migration.
boost::beast::http::response<boost::beast::http::string_body> handleApplicationRequest(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls);

std::optional<std::string> generateUniqueSlug(
    const ServerConfig& config,
    const std::function<std::string(uint32_t)>& generator = url_shortener::generateSlug);

// Helpers for sessions and server
std::string resolveRequestId(const boost::beast::http::request<boost::beast::http::string_body>& req, const ServerConfig& config);
std::string requestIdFromRequest(const boost::beast::http::request<boost::beast::http::string_body>& req);
std::string routeLabelFor(const std::string& target);
void recordHttpMetric(const boost::beast::http::request<boost::beast::http::string_body>& req, const std::string& route_label, const unsigned status);
void logStructured(const std::string& level, const std::string& msg, const std::map<std::string, std::string>& fields);
void configureTlsVersion(boost::asio::ssl::context& context, const std::string& min_version);
uint64_t certExpiryDaysRemaining(const std::string& cert_path);
void validatePrivateKeyFilePermissions(const std::string& key_file);

} // namespace url_shortener
