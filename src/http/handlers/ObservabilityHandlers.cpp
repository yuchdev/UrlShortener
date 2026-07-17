#include <url_shortener/http/handlers/ObservabilityHandlers.hpp>

#include <url_shortener/http/request_handlers.h>

namespace url_shortener {
namespace http {

BeastResponse handleHealthz(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    (void)context;
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        "ok\n",
        "text/plain");
}

BeastResponse handleReadyz(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    (void)context;
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        "ok\n",
        "text/plain");
}

BeastResponse handleMetrics(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    (void)context;
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        url_shortener::renderMetrics(),
        "text/plain");
}

} // namespace http
} // namespace url_shortener
