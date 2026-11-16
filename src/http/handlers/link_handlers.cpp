#include <algorithm>
#include <chrono>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <url_shortener/app/link_backend.hpp>
#include <url_shortener/commands/args_builder.hpp>
#include <url_shortener/composition/link_command_service_factory.hpp>
#include <url_shortener/core/utils.hpp>
#include <url_shortener/http/analytics_stats_handler.hpp>
#include <url_shortener/http/command_bridge.hpp>
#include <url_shortener/http/handlers/link_handlers.hpp>
#include <url_shortener/http/request_handlers.hpp>
#include <url_shortener/storage/errors/rate_limit_error.hpp>
#include <url_shortener/storage/i_rate_limiter.hpp>
#include <url_shortener/storage/models/rate_limit_decision.hpp>

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

/**
 * @brief Runs one read-only resource command through the command bridge and
 * renders its outcome as the exact HTTP response the route returns today.
 *
 * The single seam every migrated read handler collapses onto (milestone 0006
 * contract C1): it hands the already-built `args` object to
 * @ref execute_resource_command as the legacy Admin caller (threat model T-60
 * status quo for these open resource routes) and renders the result with
 * @ref resource_response at HTTP 200 for a success. The handler itself no
 * longer touches `app::` DTO types, the stats reader, or any error-code switch;
 * status codes, bodies and error codes are produced entirely by the bridge
 * (contract C3). The redirect fast path is unaffected (contract C10): this is
 * the management plane.
 *
 * @param req Incoming request; supplies the HTTP version and the validated
 * request id echoed into the response.
 * @param config Server configuration threaded to the response helpers; the
 * command runtime itself is process-wide and config-bound at startup.
 * @param is_tls `true` on HTTPS connections; gates HSTS/`nosniff` on the
 * rendered response.
 * @param command The registry command name to execute (e.g. `"link.get"`).
 * @param args The assembled `args` object; its JSON text is moved into the
 * request verbatim. Taken by reference because the caller owns it.
 * @return The fully-rendered Beast response: a 200 success body on success, or
 * the bridge's byte-identical legacy error envelope on failure.
 */
BeastResponse run_read_command(const BeastRequest& req,
                               const ::ServerConfig& config,
                               const bool is_tls,
                               const std::string_view command,
                               commands::args_builder& args)
{
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(
            config, command, args.str(), requestIdFromRequest(req)),
        200);
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
                                    const CreateSlugFieldMode slug_mode,
                                    const RouteContext& context)
{
    // --- REST shape validation (public contract C3: codes and their ORDER are
    // locked; the quirky extractJson* scanners are reused unchanged). Each
    // check captures a local instead of a DTO field; the args are rebuilt from
    // only these known keys afterwards, so unknown top-level keys are ignored.

    auto raw_url = url_shortener::extractJsonStringField(req.body(), "url");
    if (!raw_url.has_value()) {
        raw_url =
            url_shortener::extractJsonStringField(req.body(), "target_url");
    }
    if (!raw_url.has_value()) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 400, "invalid_url", "url is required");
    }

    auto slug = url_shortener::extractJsonStringField(req.body(), "slug");
    if (!slug.has_value() && slug_mode == CreateSlugFieldMode::slug_or_code) {
        slug = url_shortener::extractJsonStringField(req.body(), "code");
    }

    std::optional<std::string> redirect_type_raw;
    if (const auto raw =
            url_shortener::extractJsonStringField(req.body(), "redirect_type");
        raw.has_value())
    {
        // Only "temporary"/"permanent" validate; the raw value is passed on
        // verbatim (the registry enum arg accepts exactly these two strings).
        if (!url_shortener::parseRedirectType(*raw).has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_redirect_type",
                "redirect_type must be temporary or permanent");
        }
        redirect_type_raw = *raw;
    }

    std::optional<std::string> expires_at_raw;
    if (const auto raw =
            url_shortener::extractJsonStringField(req.body(), "expires_at");
        raw.has_value())
    {
        if (!url_shortener::parseRfc3339Zulu(*raw).has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_expires_at",
                "expires_at must be RFC3339 UTC");
        }
        expires_at_raw = *raw;
    }

    std::optional<bool> enabled_raw;
    if (url_shortener::hasJsonField(req.body(), "enabled")) {
        enabled_raw =
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
    }

    std::optional<std::vector<std::string>> tags;
    if (url_shortener::hasJsonField(req.body(), "tags")) {
        tags = url_shortener::extractJsonStringArrayField(req.body(), "tags");
        if (!tags.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_tags",
                "tags must be string array");
        }
    }

    std::optional<std::unordered_map<std::string, std::string>> metadata;
    if (url_shortener::hasJsonField(req.body(), "metadata")) {
        metadata = url_shortener::extractJsonFlatObjectStringField(req.body(),
                                                                   "metadata");
        if (!metadata.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_metadata",
                "metadata must be flat object with limits");
        }
    }

    bool campaign_present = false;
    bool campaign_is_null = false;
    std::string campaign_object_text;
    if (url_shortener::hasJsonField(req.body(), "campaign")) {
        campaign_present = true;
        const auto raw_campaign =
            url_shortener::extractJsonValueToken(req.body(), "campaign");
        if (raw_campaign.has_value() && *raw_campaign == "null") {
            campaign_is_null = true;
        }
        else {
            // Shape-validate exactly as before (non-object -> 400), then
            // rebuild the object from only the six known members that are
            // present, so unknown inner keys are ignored (parity) and never
            // reach the registry's object validator.
            if (!extractCampaign(req.body()).has_value()) {
                return url_shortener::makeApiErrorResponse(
                    req,
                    config,
                    is_tls,
                    400,
                    "invalid_campaign",
                    "campaign must be valid object");
            }
            commands::args_builder campaign_args;
            if (raw_campaign.has_value()) {
                static constexpr const char* k_campaign_fields[] = {
                    "name", "source", "medium", "term", "content", "id"};
                for (const char* field : k_campaign_fields) {
                    if (const auto value =
                            url_shortener::extractJsonStringField(*raw_campaign,
                                                                  field);
                        value.has_value())
                    {
                        campaign_args.add_string(field, *value);
                    }
                }
            }
            campaign_object_text = campaign_args.str();
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

    // --- Create throttle (threat model T-75), applied first in the tail,
    // before building args and before execute. Keyed on the TCP peer address
    // only (never a header). Skipped when no limiter is wired, the request
    // carries no peer, or the limit is disabled (0). An over-limit decision
    // (!allowed AND no error) is a 429; a limiter failure (error set) is
    // fail-open and falls through to the create.
    if (const auto limiter = create_rate_limiter(); limiter && context.peer
        && config.link_create_rate_limit > 0)
    {
        RateLimitError rl_error = RateLimitError::none;
        const RateLimitDecision decision = limiter->Allow(
            context.peer->to_string(),
            config.link_create_rate_limit,
            std::chrono::seconds(config.link_create_rate_window_seconds),
            &rl_error);
        if (!decision.allowed && rl_error == RateLimitError::none) {
            return url_shortener::makeApiErrorResponse(
                req, config, is_tls, 429, "rate_limited", "Too many requests");
        }
    }

    // --- Build the link.create args from the validated locals and run it
    // through the command bridge as the legacy Admin caller (threat model
    // T-60). target_url is the canonical arg; the `url` alias lives only here.
    commands::args_builder args;
    args.add_string("target_url", *raw_url);
    if (slug.has_value()) {
        args.add_string("slug", *slug);
    }
    if (redirect_type_raw.has_value()) {
        args.add_string("redirect_type", *redirect_type_raw);
    }
    if (expires_at_raw.has_value()) {
        args.add_string("expires_at", *expires_at_raw);
    }
    if (enabled_raw.has_value()) {
        args.add_bool("enabled", *enabled_raw);
    }
    if (tags.has_value()) {
        args.add_string_list("tags", *tags);
    }
    if (metadata.has_value()) {
        args.add_string_map(
            "metadata",
            std::map<std::string, std::string>(metadata->begin(),
                                               metadata->end()));
    }
    if (campaign_present) {
        if (campaign_is_null) {
            args.add_null("campaign");
        }
        else {
            // campaign_object_text is produced by a second args_builder, so it
            // is a single valid JSON object (add_raw's contract).
            args.add_raw("campaign", campaign_object_text);
        }
    }

    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(
            config, "link.create", args.str(), requestIdFromRequest(req)),
        201);
}

BeastResponse handleCreateLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               const bool is_tls,
                               const RouteContext& context)
{
    return createLinkFromRequest(
        req, config, is_tls, CreateSlugFieldMode::slug_only, context);
}

BeastResponse handleGetLinkById(const BeastRequest& req,
                                const ::ServerConfig& config,
                                const bool is_tls,
                                const RouteContext& context)
{
    // Address the link by its opaque id through link.get; the registry's
    // slug_or_id one-of group accepts the single `id` selector and the service
    // resolves it via the indexed findById lookup.
    commands::args_builder args;
    args.add_string("id", pathValue(context, "id"));
    return run_read_command(req, config, is_tls, "link.get", args);
}

BeastResponse handleGetLinkBySlug(const BeastRequest& req,
                                  const ::ServerConfig& config,
                                  const bool is_tls,
                                  const RouteContext& context)
{
    // Address the link by slug through link.get; the registry's slug_or_id
    // one-of group accepts the single `slug` selector.
    commands::args_builder args;
    args.add_string("slug", pathValue(context, "slug"));
    return run_read_command(req, config, is_tls, "link.get", args);
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
    //
    // REST contract (C3): existence is checked BEFORE any body validation so a
    // PATCH to an unknown slug returns 404 not_found even when the body is
    // malformed. The lookup runs through LinkCommandService::GetLink to avoid
    // reintroducing direct getLinkForRead calls in this migrated handler.
    app::UpdateLinkCommand command;
    command.slug = pathValue(context, "slug");

    const auto command_service = makeCommandService(config);
    if (const auto existing = command_service.service->GetLink(
            {app::GetLinkBy::slug, command.slug});
        !existing.ok())
    {
        return appErrorResponse(req, config, is_tls, existing.error);
    }

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
    // Read-only lookup runs through the link.preview command; the reduced
    // preview body is produced by the shared app::serializeLinkPreviewJson
    // serializer behind the command handler, so the REST and CLI `preview`
    // outputs stay byte-for-byte identical (single source of truth).
    commands::args_builder args;
    args.add_string("slug", pathValue(context, "slug"));
    return run_read_command(req, config, is_tls, "link.preview", args);
}

BeastResponse handlePlaceholderFeature(const BeastRequest& req,
                                       const ::ServerConfig& config,
                                       const bool is_tls,
                                       const RouteContext& context)
{
    const auto slug = pathValue(context, "slug");
    const auto link = app::LinkBackend::store().findBySlug(slug);
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
    // Range vs lifetime mode selection mirrors the old handler exactly: only
    // when from, to and bucket are all present and non-empty does link.stats
    // run the range aggregation; otherwise the command falls back to the
    // six-key lifetime body. getQueryParam keeps its documented quirks (naive
    // `key=` substring, first match, no percent-decoding) on purpose. The
    // T-76 bucket-count cap lives in the shared stats reader, so an over-cap
    // span returns range_too_large identically for REST and link.stats.
    commands::args_builder args;
    args.add_string("slug", pathValue(context, "slug"));
    const std::string from_param = getQueryParam(context.query_string, "from");
    const std::string to_param = getQueryParam(context.query_string, "to");
    const std::string bucket_param =
        getQueryParam(context.query_string, "bucket");
    if (!from_param.empty() && !to_param.empty() && !bucket_param.empty()) {
        args.add_string("from", from_param);
        args.add_string("to", to_param);
        args.add_string("bucket", bucket_param);
    }
    return run_read_command(req, config, is_tls, "link.stats", args);
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
