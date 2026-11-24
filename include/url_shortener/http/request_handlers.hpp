#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/http.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>
#include <url_shortener/core/utils.hpp>

namespace url_shortener
{

/**
 * @brief Per-request correlation data populated by a session before
 * it dispatches the request to `handleShortenerRequest`.
 */
struct RequestContext
{
    ///< Request correlation identifier.
    std::string request_id;

    ///< Normalized route label for metrics/logging.
    std::string route_label;

    ///< Request start timestamp.
    std::chrono::steady_clock::time_point started_at;
};

/**
 * @brief Process-global HTTP metrics counters, accessed via the
 * `metricsRegistry()` singleton.
 *
 * Thread-safety: the five `std::atomic` counters may be incremented and
 * read concurrently without holding `mutex`.  `http_requests_total` is
 * guarded by `mutex` and must only be accessed while holding it.
 */
struct MetricsRegistry
{
    ///< Current inflight HTTP requests.
    std::atomic<uint64_t> inflight_requests {0};

    ///< HTTP parser error count.
    std::atomic<uint64_t> parse_errors_total {0};

    ///< Malformed request count.
    std::atomic<uint64_t> malformed_requests_total {0};

    ///< Successful TLS reloads.
    std::atomic<uint64_t> tls_reload_success_total {0};

    ///< Failed TLS reloads.
    std::atomic<uint64_t> tls_reload_failure_total {0};

    ///< Create requests rejected by the create throttle (HTTP 429
    ///< `rate_limited`, over-limit). Peer-free signal: the counter never
    ///< carries the client address or key (threat model T-70/T-84).
    std::atomic<uint64_t> create_throttle_rejected_total {0};

    ///< Create requests that failed open because the rate limiter reported
    ///< a backend error (e.g. Redis down). A sustained non-zero rate
    ///< indicates the throttle is not being enforced. Peer-free.
    std::atomic<uint64_t> create_throttle_fail_open_total {0};

    ///< Protects http_requests_total map.
    std::mutex mutex;

    ///< Per-route/status request counters.
    std::map<std::string, uint64_t> http_requests_total;
};

/**
 * @brief Returns the process-global `MetricsRegistry` singleton.
 *
 * The singleton is initialized on first call and lives for the process
 * lifetime.  Safe to call from any thread.
 * @return Reference to the singleton registry.
 */
MetricsRegistry& metricsRegistry();

/**
 * @brief Serializes all current metric counters to a Prometheus-
 * compatible plaintext format.
 *
 * Renders `inflight_requests`, `parse_errors_total`,
 * `malformed_requests_total`, `tls_reload_success_total`,
 * `tls_reload_failure_total`, and each `http_requests_total` entry as
 * `http_requests_total{method=...,route=...,status_class=...} N`.
 * @return Newline-terminated text suitable for a `GET /metrics` response.
 */
std::string renderMetrics();

/**
 * @brief Builds a Beast HTTP response with standard server headers.
 *
 * Always sets `Server: simple-http`, disables keep-alive, echoes the
 * `X-Request-Id`, and optionally sets HSTS and `X-Content-Type-Options`
 * headers for TLS connections.
 * @param req          Incoming request; used to mirror HTTP version and
 *        read the `X-Internal-Request-Id` header.
 * @param config       Server configuration; `hsts_max_age` controls the
 *        HSTS header value when present.
 * @param is_tls       `true` on HTTPS connections; gates HSTS and
 *        `X-Content-Type-Options: nosniff`.
 * @param status       HTTP status code for the response.
 * @param body         Response body string.
 * @param content_type `Content-Type` header value; not set when empty.
 * @return The fully-constructed response with `prepare_payload()` called.
 */
boost::beast::http::response<boost::beast::http::string_body> makeResponse(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned int status,
    const std::string& body,
    const std::string& content_type);

/**
 * @brief Builds a JSON error envelope response with the standard
 * `{"error":{"code":"...","message":"...","request_id":"..."}}` shape.
 * @param req     Incoming request; used for version mirroring and
 *        request-ID propagation.
 * @param config  Server configuration passed to `makeResponse`.
 * @param is_tls  `true` on HTTPS connections.
 * @param status  HTTP status code.
 * @param code    Machine-readable error code (e.g. `"not_found"`).
 * @param message Human-readable error description.
 * @return A `Content-Type: application/json` response with the error
 *         envelope body.
 */
boost::beast::http::response<boost::beast::http::string_body>
makeApiErrorResponse(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    const unsigned status,
    const std::string& code,
    const std::string& message);

/**
 * @brief Builds a 308 Permanent Redirect response targeting the HTTPS
 * equivalent of the current request.
 *
 * Used when `config.http_redirect_to_https` is set and the request
 * arrived on the plain HTTP listener.  Strips the port from the `Host`
 * header and appends `config.tls.port` only when it differs from 443.
 * @param req    Incoming plain-HTTP request; `Host` and `target()` are
 *        used to construct the HTTPS `Location` URL.
 * @param config Server configuration; `config.tls.port` determines the
 *        target HTTPS port.
 * @return A 308 response with the `Location` header set.
 */
boost::beast::http::response<boost::beast::http::string_body>
makeRedirectResponse(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config);

/**
 * @brief Main request dispatcher; delegates to the application router.
 *
 * Dispatches `req` through the function-local singleton router built by
 * `RouterBuilder::buildApplicationRouter()`.  All routing decisions are
 * made by the router; this function is intentionally thin.
 * @param req    Incoming request with the `X-Internal-Request-Id` header
 *        already set by the calling session.
 * @param config Server-wide runtime configuration.
 * @param is_tls `true` on HTTPS connections.
 * @param peer   TCP peer (client) address of the connection, forwarded to
 *        `Router::dispatch` and onto `RouteContext::peer`. Defaulted to
 *        `std::nullopt` so every existing call site (tests, tools) is
 *        behavior-neutral: an absent peer means "no peer" to the gates that
 *        read it. Never logged (client PII, threat model T-70).
 * @return The response produced by the matched route handler, or a router
 *         error response (400/404) when no route matches.
 */
boost::beast::http::response<boost::beast::http::string_body>
handleShortenerRequest(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls,
    std::optional<boost::asio::ip::address> peer = std::nullopt);

/**
 * @brief Whether a request to `target` needs its TCP peer resolved.
 *
 * Only the management plane (`/api/...`) consumes `RouteContext::peer`
 * (the create throttle keys on it). The redirect fast path
 * (`GET /{slug}`, `GET /r/{slug}`) must not resolve the peer at all -
 * calling `remote_endpoint()` on it would add a syscall to the hot path
 * and contradicts contract C10 / threat model T-83 ("redirect path must
 * not resolve the peer"). The session layer calls this before deciding
 * whether to read `remote_endpoint()`; a `false` result keeps the
 * redirect path syscall-free and leaves `RouteContext::peer` empty.
 * @param target Raw request target (path + optional query), e.g. the
 *        value of `req.target()`.
 * @return `true` when `target` begins with `"/api/"` (management plane);
 *         `false` for every other target, including the redirect paths.
 */
bool targetRequiresPeer(std::string_view target);

/**
 * @brief Implements the legacy URI-store fallback.
 *
 * Handles GET (lookup), POST (save), and DELETE (remove) against the
 * in-process `UriMapSingleton`.  Router fallback handlers wrap this
 * function so the storage behavior remains centralized during route
 * migration.  Any method other than GET, POST, or DELETE returns 400.
 * @param req    Incoming request; `method()` and `target()` drive the
 *        URI-store operation.
 * @param config Server configuration passed to response helpers.
 * @param is_tls `true` on HTTPS connections.
 * @return 200 on successful GET/POST/DELETE; 404 if the URI is not found
 *         for GET or DELETE; 400 for unsupported HTTP methods.
 */
boost::beast::http::response<boost::beast::http::string_body>
handleApplicationRequest(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config,
    const bool is_tls);

/**
 * @brief Attempts to generate a unique slug for a new short link.
 *
 * Calls `generator` up to 20 times with
 * `config.shortener_generated_slug_length` as the length argument,
 * skipping any candidate that is system-reserved (`isReservedSlug`) or
 * already in use (`linkRepository().slugExists`).
 * @param config    Server configuration; `shortener_generated_slug_length`
 *        controls the generated slug length.
 * @param generator Callable producing a random slug of the given length;
 *        defaults to `url_shortener::generateSlug`.
 * @return The first valid unique candidate; `std::nullopt` if all 20
 *         attempts produced reserved or already-used slugs.
 */
std::optional<std::string> generateUniqueSlug(
    const ServerConfig& config,
    const std::function<std::string(uint32_t)>& generator =
        url_shortener::generateSlug);

/**
 * @brief Resolves the correlation request ID for an incoming request.
 *
 * Reads the `X-Request-Id` header; if it passes the allowed-character
 * and length checks (`config.request_id_max_length`), returns it as-is.
 * Otherwise generates a 32-hex-character random ID.  Invalid non-empty
 * IDs are sampled and logged as warnings.
 * @param req    Incoming request to read `X-Request-Id` from.
 * @param config Configuration supplying `request_id_max_length`.
 * @return The caller-supplied ID when valid; a freshly generated
 *         random ID otherwise.
 */
std::string resolveRequestId(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const ServerConfig& config);

/**
 * @brief Reads the internally-propagated correlation ID from the request.
 *
 * Sessions write the resolved request ID to the internal
 * `X-Internal-Request-Id` header before dispatching; this function reads
 * it back for use inside handlers and response builders.
 * @param req Incoming request containing the `X-Internal-Request-Id`
 *        header set by the calling session.
 * @return The internal request ID if present; a freshly generated random
 *         ID when the header is absent (e.g. in unit tests).
 */
std::string requestIdFromRequest(
    const boost::beast::http::request<boost::beast::http::string_body>& req);

/**
 * @brief Classifies a request target into a stable metrics/logging label.
 *
 * Delegates to `http::routeLabelForTarget` as the single source of truth
 * for label classification.
 * @param target Raw request target (path with optional query string).
 * @return One of: `metrics`, `healthz`, `readyz`, `redirect_prefixed`,
 *         `api_links`, `redirect_root`, `app`.
 * @see http::routeLabelForTarget
 */
std::string routeLabelFor(const std::string& target);

/**
 * @brief Increments the per-route/method/status-class request counter.
 *
 * Builds the label key `"method=M,route=R,status_class=Nxx"` and
 * increments the corresponding entry in `metricsRegistry().http_requests_total`
 * under `metricsRegistry().mutex`.
 * @param req         Incoming request; `method_string()` supplies the
 *        method component of the key.
 * @param route_label Stable route label (e.g. `"api_links"`).
 * @param status      HTTP response status code; the `status_class` key
 *        component is derived as `status / 100 + "xx"`.
 */
void recordHttpMetric(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const std::string& route_label,
    const unsigned status);

/**
 * @brief Writes a structured log line using `LoggerFactory::Component`.
 *
 * Formats a `ts=... level=... msg=... key=value ...` line, sanitizing
 * field values (strips control characters, truncates to 160 characters).
 * @param level  Log level string (e.g. `"INFO"`, `"WARN"`, `"ERROR"`).
 * @param msg    Primary log message.
 * @param fields Additional structured key-value pairs; values are
 *        sanitized before output.
 */
void logStructured(const std::string& level,
                   const std::string& msg,
                   const std::map<std::string, std::string>& fields);

/**
 * @brief Configures the minimum TLS protocol version on `context`.
 *
 * Always sets the maximum to TLS 1.3.  Sets the minimum to TLS 1.3 when
 * `min_version == "TLS1.3"`; otherwise keeps TLS 1.2 as the minimum.
 * @param context     SSL context to configure; modified in place.
 * @param min_version Minimum TLS version string; `"TLS1.3"` raises the
 *        minimum to TLS 1.3; any other value leaves it at TLS 1.2.
 */
void configureTlsVersion(boost::asio::ssl::context& context,
                         const std::string& min_version);

/**
 * @brief Returns the number of whole days until a PEM certificate expires.
 * @param cert_path Path to the PEM certificate file.
 * @return Days until the `notAfter` field, clamped to zero if already
 *         expired.
 * @throws `std::runtime_error` if the file cannot be opened, is not a
 *         valid PEM X.509 certificate, or the expiry time cannot be
 *         computed.
 */
uint64_t certExpiryDaysRemaining(const std::string& cert_path);

/**
 * @brief Validates that a private key file is not group- or
 * world-readable.
 *
 * A no-op on non-POSIX platforms (Windows).
 * @param key_file Path to the private key file.
 * @throws `std::runtime_error` on POSIX if the file has group-read or
 *         other-read permission bits set.
 */
void validatePrivateKeyFilePermissions(const std::string& key_file);

}  // namespace url_shortener
