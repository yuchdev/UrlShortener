#include <algorithm>
#include <chrono>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/utils.h>
#include <url_shortener/http/analytics_stats_handler.hpp>
#include <url_shortener/http/handlers/link_handlers.hpp>
#include <url_shortener/http/request_handlers.h>
#include <url_shortener/storage/link_repository.h>

#include "url_shortener/storage/memory/in_memory_click_event_repository.hpp"

namespace url_shortener
{

namespace storage::memory
{
class InMemoryClickEventRepository;
}

storage::memory::InMemoryClickEventRepository& analyticsClickRepository();

namespace http
{
namespace
{

std::string pathValue(const RouteContext& context, const std::string& name)
{
    const auto value = pathParam(context, name);
    return value.has_value() ? *value : std::string {};
}

std::string getQueryParam(const std::string& query_string,
                          const std::string& key)
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

std::optional<Link::Campaign> extractCampaign(const std::string& body)
{
    const auto raw = url_shortener::extractJsonValueToken(body, "campaign");
    if (!raw.has_value()) {
        return std::nullopt;
    }
    if (*raw == "null") {
        return Link::Campaign {};
    }
    if (raw->size() < 2 || raw->front() != '{' || raw->back() != '}') {
        return std::nullopt;
    }

    Link::Campaign campaign;
    if (const auto value = url_shortener::extractJsonStringField(*raw, "name");
        value.has_value())
    {
        campaign.name = *value;
    }
    if (const auto value =
            url_shortener::extractJsonStringField(*raw, "source");
        value.has_value())
    {
        campaign.source = *value;
    }
    if (const auto value =
            url_shortener::extractJsonStringField(*raw, "medium");
        value.has_value())
    {
        campaign.medium = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "term");
        value.has_value())
    {
        campaign.term = *value;
    }
    if (const auto value =
            url_shortener::extractJsonStringField(*raw, "content");
        value.has_value())
    {
        campaign.content = *value;
    }
    if (const auto value = url_shortener::extractJsonStringField(*raw, "id");
        value.has_value())
    {
        campaign.id = *value;
    }
    return campaign;
}

app::LinkCommandServiceBundle makeCommandService(const ::ServerConfig& config)
{
    return app::BuildLegacyLinkCommandService(config);
}

unsigned statusForAppError(const app::AppErrorCode code)
{
    switch (code) {
        case app::AppErrorCode::reserved_slug:
        case app::AppErrorCode::slug_conflict:
            return 409;
        case app::AppErrorCode::not_found:
            return 404;
        case app::AppErrorCode::storage_failure:
        case app::AppErrorCode::internal:
            return 500;
        case app::AppErrorCode::invalid_url:
        case app::AppErrorCode::invalid_slug:
        case app::AppErrorCode::invalid_field:
        case app::AppErrorCode::none:
            return 400;
    }
    return 500;
}

std::string codeForAppError(const app::AppError& error)
{
    if (error.detail == "invalid_timestamp" || error.detail == "invalid_bucket")
    {
        return error.detail;
    }
    switch (error.code) {
        case app::AppErrorCode::invalid_url:
            return "invalid_url";
        case app::AppErrorCode::invalid_slug:
            return "invalid_slug";
        case app::AppErrorCode::reserved_slug:
            return "reserved_slug";
        case app::AppErrorCode::slug_conflict:
            return "slug_conflict";
        case app::AppErrorCode::not_found:
            return "not_found";
        case app::AppErrorCode::storage_failure:
            return "repository";
        case app::AppErrorCode::invalid_field:
            return "invalid_request";
        case app::AppErrorCode::internal:
        case app::AppErrorCode::none:
            return "internal_error";
    }
    return "internal_error";
}

BeastResponse appErrorResponse(const BeastRequest& req,
                               const ::ServerConfig& config,
                               const bool is_tls,
                               const app::AppError& error)
{
    return url_shortener::makeApiErrorResponse(req,
                                               config,
                                               is_tls,
                                               statusForAppError(error.code),
                                               codeForAppError(error),
                                               error.detail);
}

BeastResponse linkNotFound(const BeastRequest& req,
                           const ::ServerConfig& config,
                           const bool is_tls)
{
    return url_shortener::makeApiErrorResponse(
        req, config, is_tls, 404, "not_found", "Link not found");
}

BeastResponse handleLifecycleAction(const BeastRequest& req,
                                    const ::ServerConfig& config,
                                    const bool is_tls,
                                    const RouteContext& context,
                                    const std::string& action)
{
    // Transport concern: translate the lifecycle route into the transport-
    // agnostic command DTO. enable/disable collapse onto SetLinkEnabled with
    // the target flag; restore maps to RestoreLink. The read-mutate-persist
    // logic (including the not_found path) lives in LinkCommandService, the
    // sole mutation path shared with the CLI adapter.
    const auto slug = pathValue(context, "slug");
    const auto command_service = makeCommandService(config);

    const auto result = action == "restore"
        ? command_service.service->RestoreLink(app::RestoreLinkCommand {slug})
        : command_service.service->SetLinkEnabled(
              app::SetLinkEnabledCommand {slug, action == "enable"});
    if (!result.ok()) {
        return appErrorResponse(req, config, is_tls, result.error);
    }
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        app::serializeLinkViewJson(*result.value),
        "application/json");
}

}  // namespace

BeastResponse createLinkFromRequest(const BeastRequest& req,
                                    const ::ServerConfig& config,
                                    const bool is_tls,
                                    const CreateSlugFieldMode slug_mode)
{
    auto raw_url = url_shortener::extractJsonStringField(req.body(), "url");
    if (!raw_url.has_value()) {
        raw_url =
            url_shortener::extractJsonStringField(req.body(), "target_url");
    }
    if (!raw_url.has_value()) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 400, "invalid_url", "url is required");
    }

    app::CreateLinkCommand command;
    command.target_url = *raw_url;
    command.slug = url_shortener::extractJsonStringField(req.body(), "slug");
    if (!command.slug.has_value()
        && slug_mode == CreateSlugFieldMode::slug_or_code)
    {
        command.slug =
            url_shortener::extractJsonStringField(req.body(), "code");
    }

    if (const auto redirect_type_raw =
            url_shortener::extractJsonStringField(req.body(), "redirect_type");
        redirect_type_raw.has_value())
    {
        const auto parsed =
            url_shortener::parseRedirectType(*redirect_type_raw);
        if (!parsed.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_redirect_type",
                "redirect_type must be temporary or permanent");
        }
        command.redirect_type = *parsed;
    }

    if (const auto expires_at_raw =
            url_shortener::extractJsonStringField(req.body(), "expires_at");
        expires_at_raw.has_value())
    {
        if (!url_shortener::parseRfc3339Zulu(*expires_at_raw).has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_expires_at",
                "expires_at must be RFC3339 UTC");
        }
        command.expires_at = *expires_at_raw;
    }

    if (url_shortener::hasJsonField(req.body(), "enabled")) {
        const auto enabled_raw =
            url_shortener::extractJsonBoolField(req.body(), "enabled");
        if (!enabled_raw.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_enabled",
                "enabled must be boolean");
        }
        command.enabled = *enabled_raw;
    }

    if (url_shortener::hasJsonField(req.body(), "tags")) {
        const auto raw_tags =
            url_shortener::extractJsonStringArrayField(req.body(), "tags");
        if (!raw_tags.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_tags",
                "tags must be string array");
        }
        command.tags = *raw_tags;
    }

    if (url_shortener::hasJsonField(req.body(), "metadata")) {
        const auto raw_metadata =
            url_shortener::extractJsonFlatObjectStringField(req.body(),
                                                            "metadata");
        if (!raw_metadata.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_metadata",
                "metadata must be flat object with limits");
        }
        command.metadata = *raw_metadata;
    }

    if (url_shortener::hasJsonField(req.body(), "campaign")) {
        const auto raw_campaign =
            url_shortener::extractJsonValueToken(req.body(), "campaign");
        if (raw_campaign.has_value() && *raw_campaign == "null") {
            command.campaign.reset();
        }
        else {
            command.campaign = extractCampaign(req.body());
            if (!command.campaign.has_value()) {
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

    const auto command_service = makeCommandService(config);
    const auto result = command_service.service->CreateLink(command);
    if (!result.ok()) {
        return appErrorResponse(req, config, is_tls, result.error);
    }

    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        201,
        app::serializeLinkViewJson(*result.value),
        "application/json");
}

BeastResponse handleCreateLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               const bool is_tls,
                               const RouteContext& context)
{
    (void)context;
    return createLinkFromRequest(
        req, config, is_tls, CreateSlugFieldMode::slug_only);
}

BeastResponse handleGetLinkById(const BeastRequest& req,
                                const ::ServerConfig& config,
                                const bool is_tls,
                                const RouteContext& context)
{
    const auto id = pathValue(context, "id");
    const auto command_service = makeCommandService(config);
    const auto result =
        command_service.service->GetLink({app::GetLinkBy::id, id});
    if (!result.ok()) {
        return appErrorResponse(req, config, is_tls, result.error);
    }
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        app::serializeLinkViewJson(*result.value),
        "application/json");
}

BeastResponse handleGetLinkBySlug(const BeastRequest& req,
                                  const ::ServerConfig& config,
                                  const bool is_tls,
                                  const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    const auto command_service = makeCommandService(config);
    const auto result =
        command_service.service->GetLink({app::GetLinkBy::slug, slug});
    if (!result.ok()) {
        return appErrorResponse(req, config, is_tls, result.error);
    }
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        app::serializeLinkViewJson(*result.value),
        "application/json");
}

BeastResponse handlePatchLink(const BeastRequest& req,
                              const ::ServerConfig& config,
                              const bool is_tls,
                              const RouteContext& context)
{
    // Transport concern: parse and shape-validate the JSON body into the
    // transport-agnostic UpdateLinkCommand DTO, preserving the exact per-field
    // 400 error codes the REST contract locks in. The read-mutate-persist and
    // business validation then run once inside LinkCommandService::UpdateLink,
    // which is also the sole path used by the CLI adapter.
    app::UpdateLinkCommand command;
    command.slug = pathValue(context, "slug");

    if (url_shortener::hasJsonField(req.body(), "enabled")) {
        const auto enabled =
            url_shortener::extractJsonBoolField(req.body(), "enabled");
        if (!enabled.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_enabled",
                "enabled must be boolean");
        }
        command.enabled = *enabled;
    }

    if (url_shortener::hasJsonField(req.body(), "expires_at")) {
        const auto expires_at =
            url_shortener::extractJsonValueToken(req.body(), "expires_at");
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
            command.expires_at = std::optional<std::string> {};
        }
        else if (const auto parsed = url_shortener::extractJsonStringField(
                     req.body(), "expires_at");
                 parsed.has_value()
                 && url_shortener::parseRfc3339Zulu(*parsed).has_value())
        {
            command.expires_at = std::optional<std::string> {*parsed};
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
        const auto tags =
            url_shortener::extractJsonStringArrayField(req.body(), "tags");
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
        command.tags = std::move(normalized);
    }

    if (url_shortener::hasJsonField(req.body(), "metadata")) {
        const auto metadata = url_shortener::extractJsonFlatObjectStringField(
            req.body(), "metadata");
        if (!metadata.has_value() || !validateMetadata(*metadata)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_metadata",
                "metadata must be flat object with string values");
        }
        command.metadata = *metadata;
    }

    if (url_shortener::hasJsonField(req.body(), "campaign")) {
        const auto raw_campaign =
            url_shortener::extractJsonValueToken(req.body(), "campaign");
        if (raw_campaign.has_value() && *raw_campaign == "null") {
            command.campaign = std::optional<Link::Campaign> {};
        }
        else {
            auto campaign = extractCampaign(req.body());
            if (!campaign.has_value()) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    400,
                    "invalid_campaign",
                    "campaign must be object or null");
            }
            if (!validateCampaign(campaign)) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    400,
                    "invalid_campaign",
                    "campaign fields exceed limits");
            }
            command.campaign = std::move(campaign);
        }
    }

    const auto command_service = makeCommandService(config);
    const auto result = command_service.service->UpdateLink(command);
    if (!result.ok()) {
        return appErrorResponse(req, config, is_tls, result.error);
    }
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        app::serializeLinkViewJson(*result.value),
        "application/json");
}

BeastResponse handleDeleteLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               const bool is_tls,
                               const RouteContext& context)
{
    app::DeleteLinkCommand command;
    command.slug = pathValue(context, "slug");

    const auto command_service = makeCommandService(config);
    const auto result = command_service.service->DeleteLink(command);
    if (!result.ok()) {
        return appErrorResponse(req, config, is_tls, result.error);
    }
    return url_shortener::makeResponse(
        req,
        config,
        is_tls,
        200,
        app::serializeLinkViewJson(*result.value),
        "application/json");
}

BeastResponse handlePreviewLink(const BeastRequest& req,
                                const ::ServerConfig& config,
                                const bool is_tls,
                                const RouteContext& context)
{
    // Read-only lookup runs through LinkCommandService::PreviewLink (no write
    // path). PreviewLink returns the full LinkView; shaping it down to the
    // reduced preview response is a transport concern kept here so the service
    // stays free of REST-specific projection.
    const auto slug = pathValue(context, "slug");
    const auto command_service = makeCommandService(config);
    const auto result =
        command_service.service->PreviewLink({app::GetLinkBy::slug, slug});
    if (!result.ok()) {
        return appErrorResponse(req, config, is_tls, result.error);
    }
    const auto& view = *result.value;
    std::ostringstream body;
    body << "{\"slug\":" << url_shortener::jsonString(view.slug)
         << ",\"url\":" << url_shortener::jsonString(view.target_url)
         << ",\"status\":"
         << url_shortener::jsonString(
                url_shortener::linkStatusToString(view.status))
         << ",\"redirect_type\":"
         << url_shortener::jsonString(
                url_shortener::redirectTypeToString(view.redirect_type))
         << ",\"enabled\":" << (view.enabled ? "true" : "false")
         << ",\"expires_at\":";
    if (view.expires_at.has_value()) {
        body << url_shortener::jsonString(*view.expires_at);
    }
    else {
        body << "null";
    }
    body << ",\"deleted_at\":";
    if (view.deleted_at.has_value()) {
        body << url_shortener::jsonString(*view.deleted_at);
    }
    else {
        body << "null";
    }
    body << '}';
    return url_shortener::makeResponse(
        req, config, is_tls, 200, body.str(), "application/json");
}

BeastResponse handlePlaceholderFeature(const BeastRequest& req,
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

BeastResponse handleLinkStats(const BeastRequest& req,
                              const ::ServerConfig& config,
                              const bool is_tls,
                              const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    const std::string from_param = getQueryParam(context.query_string, "from");
    const std::string to_param = getQueryParam(context.query_string, "to");
    const std::string bucket_param =
        getQueryParam(context.query_string, "bucket");
    if (!from_param.empty() && !to_param.empty() && !bucket_param.empty()) {
        const auto command_service = makeCommandService(config);
        const auto result = command_service.service->GetLinkStats(
            {slug, from_param, to_param, bucket_param});
        if (!result.ok()) {
            return appErrorResponse(req, config, is_tls, result.error);
        }
        return url_shortener::makeResponse(
            req,
            config,
            is_tls,
            200,
            app::serializeLinkStatsJson(*result.value),
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
    body << ",\"created_at\":" << url_shortener::jsonString(link->created_at)
         << '}';
    return url_shortener::makeResponse(
        req, config, is_tls, 200, body.str(), "application/json");
}

BeastResponse handleEnableLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               const bool is_tls,
                               const RouteContext& context)
{
    return handleLifecycleAction(req, config, is_tls, context, "enable");
}

BeastResponse handleDisableLink(const BeastRequest& req,
                                const ::ServerConfig& config,
                                const bool is_tls,
                                const RouteContext& context)
{
    return handleLifecycleAction(req, config, is_tls, context, "disable");
}

BeastResponse handleRestoreLink(const BeastRequest& req,
                                const ::ServerConfig& config,
                                const bool is_tls,
                                const RouteContext& context)
{
    return handleLifecycleAction(req, config, is_tls, context, "restore");
}

}  // namespace http
}  // namespace url_shortener
