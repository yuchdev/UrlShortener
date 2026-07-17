#include <url_shortener/http/handlers/CompatibilityHandlers.hpp>

#include <url_shortener/http/handlers/LinkHandlers.hpp>

namespace url_shortener {
namespace http {

BeastResponse handleCompatCreateLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    (void)context;
    return createLinkFromRequest(
        req,
        config,
        is_tls,
        CreateSlugFieldMode::slug_or_code);
}

BeastResponse handleCompatGetLinkBySlug(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    return handleGetLinkBySlug(req, config, is_tls, context);
}

} // namespace http
} // namespace url_shortener
