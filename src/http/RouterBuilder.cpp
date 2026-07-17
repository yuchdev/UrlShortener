#include <url_shortener/http/RouterBuilder.hpp>

#include <url_shortener/http/handlers/CompatibilityHandlers.hpp>
#include <url_shortener/http/handlers/FallbackHandlers.hpp>
#include <url_shortener/http/handlers/LinkHandlers.hpp>
#include <url_shortener/http/handlers/ObservabilityHandlers.hpp>
#include <url_shortener/http/handlers/RedirectHandlers.hpp>
#include <url_shortener/http/request_handlers.h>

namespace url_shortener {
namespace http {

namespace {

namespace bhttp = boost::beast::http;

ErrorHandlerFn applicationErrorHandler()
{
    return [](
               const BeastRequest& req,
               const ::ServerConfig& config,
               const bool is_tls,
               const unsigned status,
               const std::string& code,
               const std::string& message) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            status,
            code,
            message);
    };
}

} // namespace

Router RouterBuilder::buildApplicationRouter()
{
    Router router{applicationErrorHandler()};

    router.add(bhttp::verb::get, "/healthz", "healthz", handleHealthz);
    router.add(bhttp::verb::get, "/readyz", "readyz", handleReadyz);
    router.add(bhttp::verb::get, "/metrics", "metrics", handleMetrics);

    router.add(bhttp::verb::get, "/api/v1/links/id/{id}", "api_links", handleGetLinkById);
    router.add(bhttp::verb::get, "/api/v1/links/{slug}/preview", "api_links", handlePreviewLink);
    router.add(bhttp::verb::get, "/api/v1/links/{slug}/qr", "api_links", handlePlaceholderFeature);
    router.add(bhttp::verb::get, "/api/v1/links/{slug}/routing", "api_links", handlePlaceholderFeature);
    router.add(bhttp::verb::get, "/api/v1/links/{slug}/stats", "api_links", handleLinkStats);
    router.add(bhttp::verb::post, "/api/v1/links/{slug}/enable", "api_links", handleEnableLink);
    router.add(bhttp::verb::post, "/api/v1/links/{slug}/disable", "api_links", handleDisableLink);
    router.add(bhttp::verb::post, "/api/v1/links/{slug}/restore", "api_links", handleRestoreLink);
    router.add(bhttp::verb::get, "/api/v1/links/{slug}", "api_links", handleGetLinkBySlug);
    router.add(bhttp::verb::patch, "/api/v1/links/{slug}", "api_links", handlePatchLink);
    router.add(bhttp::verb::delete_, "/api/v1/links/{slug}", "api_links", handleDeleteLink);
    router.add(bhttp::verb::post, "/api/v1/links", "api_links", handleCreateLink);

    router.add(bhttp::verb::post, "/api/v1/short-urls", "api_links", handleCompatCreateLink);
    router.add(bhttp::verb::get, "/api/v1/short-urls/{slug}", "api_links", handleCompatGetLinkBySlug);

    router.add(bhttp::verb::get, "/r/{slug}", "redirect_prefixed", handlePrefixedRedirect);
    router.add(bhttp::verb::get, "/{slug}", "redirect_root", handleRootRedirect);

    router.add(bhttp::verb::get, "/{path}", "redirect_root", handleFallbackUriStore);
    router.add(bhttp::verb::post, "/{path}", "redirect_root", handleFallbackUriStore);
    router.add(bhttp::verb::delete_, "/{path}", "redirect_root", handleFallbackUriStore);
    router.add(bhttp::verb::get, "/", "app", handleFallbackUriStore);
    router.add(bhttp::verb::post, "/", "app", handleFallbackUriStore);
    router.add(bhttp::verb::delete_, "/", "app", handleFallbackUriStore);

    return router;
}

} // namespace http
} // namespace url_shortener
