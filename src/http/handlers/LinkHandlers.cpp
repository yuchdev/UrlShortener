#include <url_shortener/http/handlers/LinkHandlers.hpp>

#include <url_shortener/core/utils.h>
#include <url_shortener/http/AnalyticsStatsHandler.hpp>
#include <url_shortener/http/request_handlers.h>
#include <url_shortener/storage/link_repository.h>
#include "url_shortener/storage/memory/InMemoryClickEventRepository.hpp"

#include <algorithm>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace url_shortener {

namespace storage::memory {
class InMemoryClickEventRepository;
}

storage::memory::InMemoryClickEventRepository& analyticsClickRepository();

namespace http {
namespace {

std::string pathValue(const RouteContext& context, const std::string& name)
{
    const auto value = pathParam(context, name);
    return value.has_value() ? *value : std::string{};
}

std::string getQueryParam(const std::string& query_string, const std::string& key)
{
    const std::string prefix = key + "=";
    const auto pos = query_string.find(prefix);
    if (pos == std::string::npos) {
        return {};
    }
    const auto val_start = pos + prefix.size();
    const auto val_end = query_string.find('&', val_start);
    return val_end == std::string::npos
        ? query_string.substr(val_start)
        : query_string.substr(val_start, val_end - val_start);
}

std::string makeShortUrl(
    const BeastRequest&,
    const ::ServerConfig& config,
    bool,
    const std::string& slug)
{
    std::string base = config.shortener_base_domain;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + "/" + slug;
}

std::string serializeLink(const Link& link, const std::string& short_url)
{
    std::ostringstream body;
    body << "{\"id\":" << url_shortener::jsonString(link.id)
         << ",\"slug\":" << url_shortener::jsonString(link.slug)
         << ",\"url\":" << url_shortener::jsonString(link.target_url)
         << ",\"short_url\":" << url_shortener::jsonString(short_url)
         << ",\"created_at\":" << url_shortener::jsonString(link.created_at)
         << ",\"updated_at\":" << url_shortener::jsonString(link.updated_at)
         << ",\"status\":" << url_shortener::jsonString(
                url_shortener::linkStatusToString(url_shortener::resolveLinkStatus(link)))
         << ",\"redirect_type\":" << url_shortener::jsonString(
                url_shortener::redirectTypeToString(link.redirect_type))
         << ",\"tags\":[";
    for (size_t i = 0; i < link.tags.size(); ++i) {
        if (i > 0) {
            body << ',';
        }
        body << url_shortener::jsonString(link.tags[i]);
    }
    body << "],\"metadata\":{";
    if (!link.metadata.empty()) {
        std::vector<std::pair<std::string, std::string>> metadata_items(
            link.metadata.begin(),
            link.metadata.end());
        std::sort(metadata_items.begin(), metadata_items.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
        for (size_t i = 0; i < metadata_items.size(); ++i) {
            if (i > 0) {
                body << ',';
            }
            body << url_shortener::jsonString(metadata_items[i].first)
                 << ':' << url_shortener::jsonString(metadata_items[i].second);
        }
    }
    body << "},\"campaign\":";
    if (link.campaign.has_value()) {
        body << '{';
        bool first = true;
        const auto appendCampaignField = [&](const char* key, const std::optional<std::string>& value) {
            if (!value.has_value()) {
                return;
            }
            if (!first) {
                body << ',';
            }
            first = false;
            body << url_shortener::jsonString(key) << ':' << url_shortener::jsonString(*value);
        };
        appendCampaignField("name", link.campaign->name);
        appendCampaignField("source", link.campaign->source);
        appendCampaignField("medium", link.campaign->medium);
        appendCampaignField("term", link.campaign->term);
        appendCampaignField("content", link.campaign->content);
        appendCampaignField("id", link.campaign->id);
        body << '}';
    }
    else {
        body << "null";
    }
    body << ",\"stats\":{"
         << "\"total_redirects\":" << link.stats.total_redirects
         << ",\"redirects_24h\":" << link.stats.redirects_24h
         << ",\"redirects_7d\":" << link.stats.redirects_7d
         << ",\"last_accessed_at\":";
    if (link.stats.last_accessed_at.has_value()) {
        body << url_shortener::jsonString(*link.stats.last_accessed_at);
    }
    else {
        body << "null";
    }
    body << "}}";
    return body.str();
}

std::optional<Link::Campaign> extractCampaign(const std::string& body)
{
    const auto raw = url_shortener::extractJsonValueToken(body, "campaign");
    if (!raw.has_value()) {
        return std::nullopt;
    }
    if (*raw == "null") {
        return Link::Campaign{};
    }
    if (raw->size() < 2 || raw->front() != '{' || raw->back() != '}') {
        return std::nullopt;
    }

    Link::Campaign campaign;
    if (const auto value = url_shortener::extractJsonStringField(*raw, "name"); value.has_value()) {
        campaign.name = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "source"); value.has_value()) {
        campaign.source = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "medium"); value.has_value()) {
        campaign.medium = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "term"); value.has_value()) {
        campaign.term = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "content"); value.has_value()) {
        campaign.content = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "id"); value.has_value()) {
        campaign.id = *value;
    }
    return campaign;
}

BeastResponse linkNotFound(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls)
{
    return url_shortener::makeApiErrorResponse(
        req,
        config,
        is_tls,
        404,
        "not_found",
        "Link not found");
}

BeastResponse handleLifecycleAction(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context,
    const std::string& action)
{
    const auto slug = pathValue(context, "slug");
    auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }

    if (action == "enable") {
        link->enabled = true;
    }
    else if (action == "disable") {
        link->enabled = false;
    }
    else {
        link->deleted_at.reset();
    }
    link->updated_at = url_shortener::currentTimestamp();
    url_shortener::updateLinkAndInvalidateCache(*link);
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)),
        "application/json");
}

} // namespace

BeastResponse createLinkFromRequest(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const CreateSlugFieldMode slug_mode)
{
    const auto raw_url = url_shortener::extractJsonStringField(req.body(), "url");
    if (!raw_url.has_value()) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            400,
            "invalid_url",
            "url is required");
    }
    const auto normalized_url = url_shortener::normalizeTargetUrl(*raw_url, config);
    if (!normalized_url.has_value()) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            400,
            "invalid_url",
            "url must be an absolute http/https URL");
    }

    std::string slug;
    if (const auto custom_slug = url_shortener::extractJsonStringField(req.body(), "slug"); custom_slug.has_value()) {
        if (!url_shortener::isValidSlug(*custom_slug)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_slug",
                "slug format is invalid");
        }
        if (url_shortener::isReservedSlug(*custom_slug)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                409,
                "reserved_slug",
                "slug is reserved");
        }
        if (linkRepository().slugExists(*custom_slug)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                409,
                "slug_conflict",
                "slug already in use");
        }
        slug = *custom_slug;
    }
    else if (slug_mode == CreateSlugFieldMode::slug_or_code) {
        if (const auto compat_code = url_shortener::extractJsonStringField(req.body(), "code"); compat_code.has_value()) {
            if (!url_shortener::isValidSlug(*compat_code)) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    400,
                    "invalid_slug",
                    "slug format is invalid");
            }
            if (url_shortener::isReservedSlug(*compat_code)) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    409,
                    "reserved_slug",
                    "slug is reserved");
            }
            if (linkRepository().slugExists(*compat_code)) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    409,
                    "slug_conflict",
                    "slug already in use");
            }
            slug = *compat_code;
        }
    }
    if (slug.empty()) {
        const auto generated_slug = generateUniqueSlug(config);
        if (!generated_slug.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                500,
                "slug_generation_failed",
                "Unable to generate a unique slug");
        }
        slug = *generated_slug;
    }

    RedirectType redirect_type = RedirectType::temporary;
    if (const auto redirect_type_raw = url_shortener::extractJsonStringField(req.body(), "redirect_type"); redirect_type_raw.has_value()) {
        const auto parsed = url_shortener::parseRedirectType(*redirect_type_raw);
        if (!parsed.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_redirect_type",
                "redirect_type must be temporary or permanent");
        }
        redirect_type = *parsed;
    }
    else if (const auto parsed = url_shortener::parseRedirectType(config.shortener_default_redirect_type); parsed.has_value()) {
        redirect_type = *parsed;
    }

    std::optional<std::string> expires_at;
    if (const auto expires_at_raw = url_shortener::extractJsonStringField(req.body(), "expires_at"); expires_at_raw.has_value()) {
        if (!url_shortener::parseRfc3339Zulu(*expires_at_raw).has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_expires_at",
                "expires_at must be RFC3339 UTC");
        }
        expires_at = *expires_at_raw;
    }
    else if (config.shortener_default_expiry_seconds.has_value()) {
        expires_at = url_shortener::formatTimestamp(
            std::chrono::system_clock::now() + std::chrono::seconds(*config.shortener_default_expiry_seconds));
    }

    bool enabled = true;
    if (url_shortener::hasJsonField(req.body(), "enabled")) {
        const auto enabled_raw = url_shortener::extractJsonBoolField(req.body(), "enabled");
        if (!enabled_raw.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_enabled",
                "enabled must be boolean");
        }
        enabled = *enabled_raw;
    }

    std::vector<std::string> tags;
    if (url_shortener::hasJsonField(req.body(), "tags")) {
        const auto raw_tags = url_shortener::extractJsonStringArrayField(req.body(), "tags");
        if (!raw_tags.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_tags",
                "tags must be string array");
        }
        tags = *raw_tags;
        if (!validateTags(tags)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_tags",
                "tags violate constraints");
        }
    }

    std::unordered_map<std::string, std::string> metadata;
    if (url_shortener::hasJsonField(req.body(), "metadata")) {
        const auto raw_metadata = url_shortener::extractJsonFlatObjectStringField(req.body(), "metadata");
        if (!raw_metadata.has_value() || !validateMetadata(*raw_metadata)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_metadata",
                "metadata must be flat object with limits");
        }
        metadata = *raw_metadata;
    }

    std::optional<Link::Campaign> campaign;
    if (url_shortener::hasJsonField(req.body(), "campaign")) {
        const auto raw_campaign = url_shortener::extractJsonValueToken(req.body(), "campaign");
        if (raw_campaign.has_value() && *raw_campaign == "null") {
            campaign.reset();
        }
        else {
            campaign = extractCampaign(req.body());
            if (!campaign.has_value() || !validateCampaign(campaign)) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    400,
                    "invalid_campaign",
                    "campaign must be valid object");
            }
        }
    }

    if (url_shortener::hasJsonField(req.body(), "deleted_at")) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            400,
            "invalid_request",
            "deleted_at is server-managed");
    }

    Link link;
    link.id = url_shortener::generateId();
    link.slug = slug;
    link.target_url = *normalized_url;
    link.created_at = url_shortener::currentTimestamp();
    link.updated_at = link.created_at;
    link.expires_at = expires_at;
    link.enabled = enabled;
    link.tags = std::move(tags);
    link.metadata = std::move(metadata);
    link.campaign = std::move(campaign);
    link.redirect_type = redirect_type;

    if (!linkRepository().create(link)) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            409,
            "slug_conflict",
            "slug already in use");
    }
    url_shortener::linkCache().erase(link.slug);

    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        201,
        serializeLink(link, makeShortUrl(req, config, is_tls, link.slug)),
        "application/json");
}

BeastResponse handleCreateLink(
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
        CreateSlugFieldMode::slug_only);
}

BeastResponse handleGetLinkById(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto id = pathValue(context, "id");
    const auto link = linkRepository().getById(id);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)),
        "application/json");
}

BeastResponse handleGetLinkBySlug(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    const auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)),
        "application/json");
}

BeastResponse handlePatchLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }

    if (url_shortener::hasJsonField(req.body(), "enabled")) {
        const auto enabled = url_shortener::extractJsonBoolField(req.body(), "enabled");
        if (!enabled.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_enabled",
                "enabled must be boolean");
        }
        link->enabled = *enabled;
    }

    if (url_shortener::hasJsonField(req.body(), "expires_at")) {
        const auto expires_at = url_shortener::extractJsonValueToken(req.body(), "expires_at");
        if (!expires_at.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_expires_at",
                "expires_at must be RFC3339 UTC or null");
        }
        if (*expires_at == "null") {
            link->expires_at.reset();
        }
        else if (const auto parsed = url_shortener::extractJsonStringField(req.body(), "expires_at");
                 parsed.has_value() && url_shortener::parseRfc3339Zulu(*parsed).has_value()) {
            link->expires_at = *parsed;
        }
        else {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_expires_at",
                "expires_at must be RFC3339 UTC or null");
        }
    }

    if (url_shortener::hasJsonField(req.body(), "tags")) {
        const auto tags = url_shortener::extractJsonStringArrayField(req.body(), "tags");
        if (!tags.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_tags",
                "tags must be string array");
        }
        auto normalized = *tags;
        if (!validateTags(normalized)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_tags",
                "tags violate constraints");
        }
        link->tags = std::move(normalized);
    }

    if (url_shortener::hasJsonField(req.body(), "metadata")) {
        const auto metadata = url_shortener::extractJsonFlatObjectStringField(req.body(), "metadata");
        if (!metadata.has_value() || !validateMetadata(*metadata)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_metadata",
                "metadata must be flat object with string values");
        }
        link->metadata = *metadata;
    }

    if (url_shortener::hasJsonField(req.body(), "campaign")) {
        const auto raw_campaign = url_shortener::extractJsonValueToken(req.body(), "campaign");
        if (raw_campaign.has_value() && *raw_campaign == "null") {
            link->campaign.reset();
        }
        else {
            const auto campaign = extractCampaign(req.body());
            if (!campaign.has_value()) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    400,
                    "invalid_campaign",
                    "campaign must be object or null");
            }
            link->campaign = *campaign;
            if (!validateCampaign(link->campaign)) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    400,
                    "invalid_campaign",
                    "campaign fields exceed limits");
            }
        }
    }

    link->updated_at = url_shortener::currentTimestamp();
    url_shortener::updateLinkAndInvalidateCache(*link);
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)),
        "application/json");
}

BeastResponse handleDeleteLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }
    link->deleted_at = url_shortener::currentTimestamp();
    link->updated_at = url_shortener::currentTimestamp();
    url_shortener::updateLinkAndInvalidateCache(*link);
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        serializeLink(*link, makeShortUrl(req, config, is_tls, link->slug)),
        "application/json");
}

BeastResponse handlePreviewLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    const auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }
    const auto status = url_shortener::resolveLinkStatus(*link);
    std::ostringstream body;
    body << "{\"slug\":" << url_shortener::jsonString(link->slug)
         << ",\"url\":" << url_shortener::jsonString(link->target_url)
         << ",\"status\":" << url_shortener::jsonString(url_shortener::linkStatusToString(status))
         << ",\"redirect_type\":" << url_shortener::jsonString(url_shortener::redirectTypeToString(link->redirect_type))
         << ",\"enabled\":" << (link->enabled ? "true" : "false")
         << ",\"expires_at\":";
    if (link->expires_at.has_value()) {
        body << url_shortener::jsonString(*link->expires_at);
    }
    else {
        body << "null";
    }
    body << ",\"deleted_at\":";
    if (link->deleted_at.has_value()) {
        body << url_shortener::jsonString(*link->deleted_at);
    }
    else {
        body << "null";
    }
    body << '}';
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        body.str(),
        "application/json");
}

BeastResponse handlePlaceholderFeature(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    const auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }
    return url_shortener::makeApiErrorResponse(
        req,
        config,
        is_tls,
        501,
        "feature_not_enabled",
        "Feature placeholder only; implementation deferred");
}

BeastResponse handleLinkStats(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    const std::string from_param = getQueryParam(context.query_string, "from");
    const std::string to_param = getQueryParam(context.query_string, "to");
    const std::string bucket_param = getQueryParam(context.query_string, "bucket");
    if (!from_param.empty() && !to_param.empty() && !bucket_param.empty()) {
        http::AnalyticsStatsHandler stats_handler(analyticsClickRepository());
        std::string body;
        int status = 500;
        stats_handler.Handle(
            slug,
            from_param,
            to_param,
            bucket_param,
            &body,
            &status,
            requestIdFromRequest(req));
        return url_shortener::makeResponse(
            req,
            config,
            is_tls,
            static_cast<unsigned>(status),
            body,
            "application/json");
    }

    const auto link = url_shortener::getLinkForRead(slug);
    if (!link.has_value()) {
        return linkNotFound(req, config, is_tls);
    }
    std::ostringstream body;
    body << "{\"slug\":" << url_shortener::jsonString(link->slug)
         << ",\"total_redirects\":" << link->stats.total_redirects
         << ",\"redirects_24h\":" << link->stats.redirects_24h
         << ",\"redirects_7d\":" << link->stats.redirects_7d
         << ",\"last_accessed_at\":";
    if (link->stats.last_accessed_at.has_value()) {
        body << url_shortener::jsonString(*link->stats.last_accessed_at);
    }
    else {
        body << "null";
    }
    body << ",\"created_at\":" << url_shortener::jsonString(link->created_at) << '}';
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        body.str(),
        "application/json");
}

BeastResponse handleEnableLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    return handleLifecycleAction(req, config, is_tls, context, "enable");
}

BeastResponse handleDisableLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    return handleLifecycleAction(req, config, is_tls, context, "disable");
}

BeastResponse handleRestoreLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    const bool is_tls,
    const RouteContext& context)
{
    return handleLifecycleAction(req, config, is_tls, context, "restore");
}

} // namespace http
} // namespace url_shortener
