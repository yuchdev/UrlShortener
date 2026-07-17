#include <url_shortener/http/handlers/FallbackHandlers.hpp>

#include <url_shortener/http/request_handlers.h>

#include <string>

namespace url_shortener {
namespace http {

namespace {

bool startsWith(const std::string& value, const std::string& prefix)
{
    return value.rfind(prefix, 0) == 0;
}

std::string pathOnly(const BeastRequest& req)
{
    const std::string target{req.target()};
    const auto query_pos = target.find('?');
    return query_pos == std::string::npos ? target : target.substr(0, query_pos);
}

bool isReservedFallbackPath(const BeastRequest& req)
{
    const auto path = pathOnly(req);
    return startsWith(path, "/api/") || startsWith(path, "/r/");
}

} // namespace

BeastResponse handleFallbackUriStore(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    (void)context;
    if (isReservedFallbackPath(req)) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            404,
            "not_found",
            "Route not found");
    }
    // Keep the legacy URI-store implementation centralized in
    // handleApplicationRequest while router migration completes.
    return url_shortener::handleApplicationRequest(req, config, is_tls);
}

} // namespace http
} // namespace url_shortener
