#include <url_shortener/http/handlers/RedirectHandlers.hpp>

#include <url_shortener/analytics/click_event_queue.h>
#include <url_shortener/core/utils.h>
#include <url_shortener/http/request_handlers.h>
#include <url_shortener/storage/link_repository.h>
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace url_shortener {

namespace storage::memory {
class InMemoryClickEventRepository;
}

storage::memory::InMemoryClickEventRepository& analyticsClickRepository();

namespace http {
namespace {

namespace bhttp = boost::beast::http;

std::string pathValue(const RouteContext& context, const std::string& name)
{
    const auto value = pathParam(context, name);
    return value.has_value() ? *value : std::string{};
}

void emitClickEvent(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const std::string& slug,
    const std::optional<Link>& link,
    const uint16_t status_code)
{
    if (!config.analytics_enabled) {
        return;
    }

    ClickEvent event;
    event.event_id = url_shortener::generateId();
    event.occurred_at = url_shortener::currentTimestamp();
    event.slug = slug;
    if (link.has_value()) {
        event.link_id = link->id;
    }

    std::string host = req.base().count(bhttp::field::host)
        ? std::string(req.base().at(bhttp::field::host))
        : "";
    if (const auto pos = host.find(':'); pos != std::string::npos) {
        host = host.substr(0, pos);
    }
    event.domain = url_shortener::clampString(host, 255);
    event.status_code = status_code;
    event.referrer = url_shortener::headerString(req, bhttp::field::referer, 512);
    event.user_agent = url_shortener::headerString(req, bhttp::field::user_agent, 512);

    const std::string forwarded_for = req.base().count("X-Forwarded-For")
        ? std::string(req.base().at("X-Forwarded-For"))
        : "";
    const std::string client_source = forwarded_for.empty()
        ? "unknown"
        : url_shortener::clampString(forwarded_for, 128);
    event.client_id_hash = url_shortener::hashClientIdentifier(
        client_source,
        config.analytics_client_hash_salt);

    analytics::ClickEvent analytics_event;
    analytics_event.event_id = event.event_id;
    analytics_event.occurred_at = std::chrono::system_clock::now();
    analytics_event.slug = slug;
    analytics_event.link_id = link.has_value() ? link->id : std::string{};
    analytics_event.domain = event.domain;
    analytics_event.status_code = status_code;
    analytics_event.referrer = event.referrer.value_or(std::string{});
    analytics_event.user_agent = event.user_agent.value_or(std::string{});
    analytics_event.client_id_hash = event.client_id_hash;
    analyticsClickRepository().InsertBatch({std::move(analytics_event)}, nullptr);

    (void)analyticsQueue(config).tryEnqueue(std::move(event));
}

BeastResponse redirectResponse(
    const BeastRequest& req,
    const Link& link,
    const bhttp::status redirect_status)
{
    BeastResponse res{redirect_status, req.version()};
    res.set(bhttp::field::server, "simple-http");
    res.set(bhttp::field::location, link.target_url);
    res.set("X-Request-Id", url_shortener::requestIdFromRequest(req));
    res.keep_alive(false);
    res.prepare_payload();
    return res;
}

void incrementRedirectStats(const Link& link)
{
    auto updated = link;
    updated.stats.total_redirects++;
    updated.stats.redirects_24h++;
    updated.stats.redirects_7d++;
    updated.stats.last_accessed_at = url_shortener::currentTimestamp();
    url_shortener::updateLinkAndInvalidateCache(updated);
}

BeastResponse linkStateError(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const std::string& slug,
    const std::optional<Link>& link)
{
    if (link.has_value()) {
        const auto status = url_shortener::resolveLinkStatus(*link);
        if (status == LinkStatus::deleted) {
            emitClickEvent(req, config, slug, link, 404);
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                404,
                "link_deleted",
                "Link not found");
        }
        if (status == LinkStatus::disabled) {
            emitClickEvent(req, config, slug, link, 410);
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                410,
                "link_disabled",
                "Link is disabled");
        }
        if (status == LinkStatus::expired) {
            emitClickEvent(req, config, slug, link, 410);
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                410,
                "link_expired",
                "Link has expired");
        }
    }

    emitClickEvent(req, config, slug, link, 404);
    return url_shortener::makeApiErrorResponse(
        req,
        config,
        is_tls,
        404,
        "not_found",
        "Link not found");
}

} // namespace

BeastResponse handlePrefixedRedirect(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    if (!url_shortener::isValidSlug(slug)) {
        emitClickEvent(req, config, slug, std::nullopt, 404);
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            404,
            "not_found",
            "Short code not found");
    }

    const auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value() || url_shortener::resolveLinkStatus(*link) != LinkStatus::active) {
        return linkStateError(req, config, is_tls, slug, link);
    }

    if (!url_shortener::passwordGuardCheck(*link, req)
        || !url_shortener::rateLimitGuardAllow(*link, req)) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            429,
            "feature_not_enabled",
            "Request blocked by policy hook");
    }

    incrementRedirectStats(*link);

    const auto redirect_status = url_shortener::redirectStatusFor(*link);
    emitClickEvent(req, config, slug, link, static_cast<uint16_t>(redirect_status));
    return redirectResponse(req, *link, redirect_status);
}

BeastResponse handleRootRedirect(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    if (req.method() != bhttp::verb::get) {
        return url_shortener::handleApplicationRequest(req, config, is_tls);
    }

    const auto slug = pathValue(context, "slug");
    if (!context.query_string.empty()) {
        return url_shortener::handleApplicationRequest(req, config, is_tls);
    }
    if (!url_shortener::isValidSlug(slug)) {
        return url_shortener::handleApplicationRequest(req, config, is_tls);
    }

    // Valid slug-shaped root paths are redirect attempts. Missing or inactive
    // links return redirect API errors instead of reading URI-store fallback data.
    const auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value() || url_shortener::resolveLinkStatus(*link) != LinkStatus::active) {
        return linkStateError(req, config, is_tls, slug, link);
    }

    if (!url_shortener::passwordGuardCheck(*link, req)
        || !url_shortener::rateLimitGuardAllow(*link, req)) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            429,
            "feature_not_enabled",
            "Request blocked by policy hook");
    }

    incrementRedirectStats(*link);

    const auto redirect_status = url_shortener::redirectStatusFor(*link);
    emitClickEvent(req, config, slug, link, static_cast<uint16_t>(redirect_status));
    return redirectResponse(req, *link, redirect_status);
}

} // namespace http
} // namespace url_shortener
