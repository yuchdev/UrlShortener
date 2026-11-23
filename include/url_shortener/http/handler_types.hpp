#pragma once

#include <functional>
#include <string>

#include <boost/beast/http.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/route_context.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Beast HTTP request type with a `string_body`, used as the
/// concrete request type throughout all handlers and routing helpers.
using BeastRequest =
    boost::beast::http::request<boost::beast::http::string_body>;

/// @brief Beast HTTP response type with a `string_body`, returned by
/// all route handlers and response-building helpers.
using BeastResponse =
    boost::beast::http::response<boost::beast::http::string_body>;

/// @brief Synchronous route-handler callable conforming to the
/// router's dispatch contract; every route registered with `Router`
/// must match this signature.
///
/// Callback parameters (in order):
/// - `req`     – Fully-parsed incoming request.
/// - `config`  – Server-wide runtime configuration; not owned by the
///               handler; the caller keeps it alive.
/// - `is_tls`  – `true` when the connection was accepted on the HTTPS
///               listener; used to gate HSTS and security headers.
/// - `context` – Route context providing captured URI template
///               parameters and the raw query string.
///
/// Every conforming callable must return a fully-constructed
/// `BeastResponse`; throwing from a handler is not permitted.
using HandlerFn = std::function<BeastResponse(const BeastRequest& req,
                                              const ::ServerConfig& config,
                                              bool is_tls,
                                              const RouteContext& context)>;

/// @brief Error-response factory callable injected into `Router` to
/// keep routing infrastructure format-agnostic.
///
/// Callback parameters (in order):
/// - `req`     – Original request (used to mirror version/keep-alive).
/// - `config`  – Server-wide runtime configuration.
/// - `is_tls`  – `true` on HTTPS connections.
/// - `status`  – HTTP status code (e.g. 404, 400).
/// - `code`    – Machine-readable error string (e.g. `"not_found"`).
/// - `message` – Human-readable error description.
using ErrorHandlerFn = std::function<BeastResponse(const BeastRequest& req,
                                                   const ::ServerConfig& config,
                                                   bool is_tls,
                                                   unsigned status,
                                                   const std::string& code,
                                                   const std::string& message)>;

}  // namespace http
}  // namespace url_shortener
