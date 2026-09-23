#include <url_shortener/app/link_command_service.hpp>

#include <url_shortener/core/utils.h>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <utility>

namespace url_shortener {
namespace app {
namespace {

std::string shortUrlFor(const ServerConfig& config, const std::string& slug)
{
    std::string base = config.shortener_base_domain;
    while (!base.empty() && base.back() == '/') {
        base.pop_back();
    }
    return base + "/" + slug;
}

AppError error(const AppErrorCode code, std::string detail)
{
    return AppError{code, std::move(detail)};
}

} // namespace

LinkCommandService::LinkCommandService(
    ILinkStore& store,
    ILinkStatsReader& stats_reader,
    const ServerConfig& config)
    : store_(store)
    , stats_reader_(stats_reader)
    , config_(config)
{
}

Result<std::string> LinkCommandService::chooseSlug(
    const CreateLinkCommand& command) const
{
    if (command.slug.has_value()) {
        if (!isValidSlug(*command.slug)) {
            return {std::nullopt, error(AppErrorCode::invalid_slug, "slug format is invalid")};
        }
        if (isReservedSlug(*command.slug)) {
            return {std::nullopt, error(AppErrorCode::reserved_slug, "slug is reserved")};
        }
        if (store_.slugExists(*command.slug)) {
            return {std::nullopt, error(AppErrorCode::slug_conflict, "slug already in use")};
        }
        return {*command.slug, {}};
    }

    for (int attempt = 0; attempt < 10; ++attempt) {
        const auto slug = generateSlug(config_.shortener_generated_slug_length);
        if (!store_.slugExists(slug) && !isReservedSlug(slug)) {
            return {slug, {}};
        }
    }

    return {std::nullopt, error(AppErrorCode::internal, "unable to generate a unique slug")};
}

Result<LinkView> LinkCommandService::CreateLink(
    const CreateLinkCommand& command) const
{
    const auto normalized_url = normalizeTargetUrl(command.target_url, config_);
    if (!normalized_url.has_value()) {
        return {std::nullopt, error(AppErrorCode::invalid_url, "url must be an absolute http/https URL")};
    }

    const auto slug = chooseSlug(command);
    if (!slug.ok()) {
        return {std::nullopt, slug.error};
    }

    RedirectType redirect_type = RedirectType::temporary;
    if (command.redirect_type.has_value()) {
        redirect_type = *command.redirect_type;
    }
    else if (const auto parsed = parseRedirectType(config_.shortener_default_redirect_type); parsed.has_value()) {
        redirect_type = *parsed;
    }

    std::optional<std::string> expires_at = command.expires_at;
    if (expires_at.has_value() && !parseRfc3339Zulu(*expires_at).has_value()) {
        return {std::nullopt, error(AppErrorCode::invalid_field, "expires_at must be RFC3339 UTC")};
    }
    if (!expires_at.has_value() && config_.shortener_default_expiry_seconds.has_value()) {
        expires_at = formatTimestamp(
            std::chrono::system_clock::now()
            + std::chrono::seconds(*config_.shortener_default_expiry_seconds));
    }

    auto tags = command.tags;
    if (!validateTags(tags)) {
        return {std::nullopt, error(AppErrorCode::invalid_field, "tags violate constraints")};
    }
    if (!validateMetadata(command.metadata)) {
        return {std::nullopt, error(AppErrorCode::invalid_field, "metadata must be flat object with limits")};
    }
    if (!validateCampaign(command.campaign)) {
        return {std::nullopt, error(AppErrorCode::invalid_field, "campaign must be valid object")};
    }

    Link link;
    link.id = generateId();
    link.slug = *slug.value;
    link.target_url = *normalized_url;
    link.created_at = currentTimestamp();
    link.updated_at = link.created_at;
    link.expires_at = expires_at;
    link.enabled = command.enabled.value_or(true);
    link.tags = std::move(tags);
    link.metadata = command.metadata;
    link.campaign = command.campaign;
    link.redirect_type = redirect_type;

    AppError store_error;
    if (!store_.create(link, &store_error)) {
        if (store_error.code == AppErrorCode::none) {
            store_error = error(AppErrorCode::slug_conflict, "slug already in use");
        }
        return {std::nullopt, store_error};
    }
    store_.invalidateCache(link.slug);
    return {toView(link), {}};
}

Result<LinkView> LinkCommandService::GetLink(const GetLinkQuery& query) const
{
    const auto link = query.by == GetLinkBy::id
        ? store_.findById(query.value)
        : store_.findBySlug(query.value);
    if (!link.has_value()) {
        return {std::nullopt, error(AppErrorCode::not_found, "Link not found")};
    }
    return {toView(*link), {}};
}

Result<LinkView> LinkCommandService::UpdateLink(
    const UpdateLinkCommand& command) const
{
    auto link = store_.findBySlug(command.slug);
    if (!link.has_value()) {
        return {std::nullopt, error(AppErrorCode::not_found, "Link not found")};
    }

    // The validateTags/validateMetadata/validateCampaign and RFC3339 checks
    // below are intentionally redundant with the REST handler's body
    // validation. The handler keeps field-specific 400 codes for the REST
    // contract; the service re-validates so non-REST callers (such as the
    // upcoming CLI adapter) cannot bypass these constraints.
    if (command.enabled.has_value()) {
        link->enabled = *command.enabled;
    }

    if (command.expires_at.has_value()) {
        const auto& value = *command.expires_at;
        if (!value.has_value()) {
            link->expires_at.reset();
        }
        else if (parseRfc3339Zulu(*value).has_value()) {
            link->expires_at = *value;
        }
        else {
            return {std::nullopt, error(AppErrorCode::invalid_field, "expires_at must be RFC3339 UTC or null")};
        }
    }

    if (command.tags.has_value()) {
        auto normalized = *command.tags;
        if (!validateTags(normalized)) {
            return {std::nullopt, error(AppErrorCode::invalid_field, "tags violate constraints")};
        }
        link->tags = std::move(normalized);
    }

    if (command.metadata.has_value()) {
        if (!validateMetadata(*command.metadata)) {
            return {std::nullopt, error(AppErrorCode::invalid_field, "metadata must be flat object with string values")};
        }
        link->metadata = *command.metadata;
    }

    if (command.campaign.has_value()) {
        const auto& value = *command.campaign;
        if (!value.has_value()) {
            link->campaign.reset();
        }
        else {
            std::optional<Link::Campaign> candidate = value;
            if (!validateCampaign(candidate)) {
                return {std::nullopt, error(AppErrorCode::invalid_field, "campaign fields exceed limits")};
            }
            link->campaign = candidate;
        }
    }

    link->updated_at = currentTimestamp();
    store_.update(*link);
    return {toView(*link), {}};
}

Result<LinkView> LinkCommandService::DeleteLink(
    const DeleteLinkCommand& command) const
{
    auto link = store_.findBySlug(command.slug);
    if (!link.has_value()) {
        return {std::nullopt, error(AppErrorCode::not_found, "Link not found")};
    }
    const auto now = currentTimestamp();
    link->deleted_at = now;
    link->updated_at = now;
    store_.update(*link);
    return {toView(*link), {}};
}

Result<LinkView> LinkCommandService::SetLinkEnabled(
    const SetLinkEnabledCommand& command) const
{
    auto link = store_.findBySlug(command.slug);
    if (!link.has_value()) {
        return {std::nullopt, error(AppErrorCode::not_found, "Link not found")};
    }
    link->enabled = command.enabled;
    link->updated_at = currentTimestamp();
    store_.update(*link);
    return {toView(*link), {}};
}

Result<LinkView> LinkCommandService::RestoreLink(
    const RestoreLinkCommand& command) const
{
    auto link = store_.findBySlug(command.slug);
    if (!link.has_value()) {
        return {std::nullopt, error(AppErrorCode::not_found, "Link not found")};
    }
    link->deleted_at.reset();
    link->updated_at = currentTimestamp();
    store_.update(*link);
    return {toView(*link), {}};
}

Result<LinkView> LinkCommandService::PreviewLink(const GetLinkQuery& query) const
{
    const auto link = query.by == GetLinkBy::id
        ? store_.findById(query.value)
        : store_.findBySlug(query.value);
    if (!link.has_value()) {
        return {std::nullopt, error(AppErrorCode::not_found, "Link not found")};
    }
    return {toView(*link), {}};
}

Result<LinkStatsView> LinkCommandService::GetLinkStats(
    const GetLinkStatsQuery& query) const
{
    return stats_reader_.read(query);
}

LinkView LinkCommandService::toView(const Link& link) const
{
    LinkView view;
    view.id = link.id;
    view.slug = link.slug;
    view.target_url = link.target_url;
    view.short_url = shortUrlFor(config_, link.slug);
    view.created_at = link.created_at;
    view.updated_at = link.updated_at;
    view.expires_at = link.expires_at;
    view.deleted_at = link.deleted_at;
    view.enabled = link.enabled;
    view.tags = link.tags;
    view.metadata = link.metadata;
    view.campaign = link.campaign;
    view.stats = link.stats;
    view.redirect_type = link.redirect_type;
    view.status = resolveLinkStatus(link);
    return view;
}

std::string serializeLinkViewJson(const LinkView& link)
{
    std::ostringstream body;
    body << "{\"id\":" << jsonString(link.id)
         << ",\"slug\":" << jsonString(link.slug)
         << ",\"url\":" << jsonString(link.target_url)
         << ",\"short_url\":" << jsonString(link.short_url)
         << ",\"created_at\":" << jsonString(link.created_at)
         << ",\"updated_at\":" << jsonString(link.updated_at)
         << ",\"status\":" << jsonString(linkStatusToString(link.status))
         << ",\"redirect_type\":" << jsonString(redirectTypeToString(link.redirect_type))
         << ",\"tags\":[";
    for (size_t i = 0; i < link.tags.size(); ++i) {
        if (i > 0) {
            body << ',';
        }
        body << jsonString(link.tags[i]);
    }
    body << "],\"metadata\":{";
    if (!link.metadata.empty()) {
        std::vector<std::pair<std::string, std::string>> items(
            link.metadata.begin(),
            link.metadata.end());
        std::sort(items.begin(), items.end(), [](const auto& lhs, const auto& rhs) {
            return lhs.first < rhs.first;
        });
        for (size_t i = 0; i < items.size(); ++i) {
            if (i > 0) {
                body << ',';
            }
            body << jsonString(items[i].first) << ':' << jsonString(items[i].second);
        }
    }
    body << "},\"campaign\":";
    if (link.campaign.has_value()) {
        body << '{';
        bool first = true;
        const auto append = [&](const char* key, const std::optional<std::string>& value) {
            if (!value.has_value()) {
                return;
            }
            if (!first) {
                body << ',';
            }
            first = false;
            body << jsonString(key) << ':' << jsonString(*value);
        };
        append("name", link.campaign->name);
        append("source", link.campaign->source);
        append("medium", link.campaign->medium);
        append("term", link.campaign->term);
        append("content", link.campaign->content);
        append("id", link.campaign->id);
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
        body << jsonString(*link.stats.last_accessed_at);
    }
    else {
        body << "null";
    }
    body << "}}";
    return body.str();
}

std::string serializeLinkPreviewJson(const LinkView& link)
{
    // Reduced preview projection shared verbatim with the REST
    // GET /api/v1/links/{slug}/preview handler (src/http/handlers/
    // link_handlers.cpp::handlePreviewLink). The field set and order here are
    // load-bearing: they are pinned byte-for-byte by the characterization tests
    // in tests/unit/http/10_link_handlers.cpp. Do not add or reorder fields
    // without updating those tests deliberately.
    std::ostringstream body;
    body << "{\"slug\":" << jsonString(link.slug)
         << ",\"url\":" << jsonString(link.target_url)
         << ",\"status\":" << jsonString(linkStatusToString(link.status))
         << ",\"redirect_type\":"
         << jsonString(redirectTypeToString(link.redirect_type))
         << ",\"enabled\":" << (link.enabled ? "true" : "false")
         << ",\"expires_at\":";
    if (link.expires_at.has_value()) {
        body << jsonString(*link.expires_at);
    }
    else {
        body << "null";
    }
    body << ",\"deleted_at\":";
    if (link.deleted_at.has_value()) {
        body << jsonString(*link.deleted_at);
    }
    else {
        body << "null";
    }
    body << '}';
    return body.str();
}

std::string serializeLinkStatsJson(const LinkStatsView& stats)
{
    std::ostringstream os;
    os << "{\"slug\":" << jsonString(stats.slug)
       << ",\"total_attempts\":" << stats.total_attempts
       << ",\"successful_redirects\":" << stats.successful_redirects
       << ",\"attempts_by_status_code\":{";
    for (size_t i = 0; i < stats.status_code_counts.size(); ++i) {
        if (i > 0) {
            os << ',';
        }
        os << '"' << stats.status_code_counts[i].status_code << "\":"
           << stats.status_code_counts[i].count;
    }
    os << "},\"attempts_by_domain\":{";
    for (size_t i = 0; i < stats.domain_counts.size(); ++i) {
        if (i > 0) {
            os << ',';
        }
        os << jsonString(stats.domain_counts[i].domain) << ':'
           << stats.domain_counts[i].count;
    }
    os << "},\"time_buckets\":[";
    for (size_t i = 0; i < stats.time_buckets.size(); ++i) {
        if (i > 0) {
            os << ',';
        }
        os << "{\"bucket_start\":" << stats.time_buckets[i].bucket_start_epoch
           << ",\"count\":" << stats.time_buckets[i].count << '}';
    }
    os << "],\"from\":" << stats.from
       << ",\"to\":" << stats.to
       << ",\"bucket\":" << jsonString(stats.bucket) << '}';
    return os.str();
}

} // namespace app
} // namespace url_shortener
