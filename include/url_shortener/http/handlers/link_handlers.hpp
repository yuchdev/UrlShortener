#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener {
namespace http {

/// Controls which request field can supply a caller-provided short code.
enum class CreateSlugFieldMode
{
    slug_only,
    slug_or_code
};

/// Creates a link from request JSON using the selected short-code field mode.
BeastResponse createLinkFromRequest(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    CreateSlugFieldMode slug_mode);

/// Handles `POST /api/v1/links`.
BeastResponse handleCreateLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `GET /api/v1/links/id/{id}`.
BeastResponse handleGetLinkById(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `GET /api/v1/links/{slug}`.
BeastResponse handleGetLinkBySlug(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `PATCH /api/v1/links/{slug}`.
BeastResponse handlePatchLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `DELETE /api/v1/links/{slug}`.
BeastResponse handleDeleteLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `GET /api/v1/links/{slug}/preview`.
BeastResponse handlePreviewLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `GET /api/v1/links/{slug}/qr` and `/routing`.
BeastResponse handlePlaceholderFeature(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `GET /api/v1/links/{slug}/stats`.
BeastResponse handleLinkStats(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `POST /api/v1/links/{slug}/enable`.
BeastResponse handleEnableLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `POST /api/v1/links/{slug}/disable`.
BeastResponse handleDisableLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

/// Handles `POST /api/v1/links/{slug}/restore`.
BeastResponse handleRestoreLink(
    const BeastRequest& req,
    const ::ServerConfig& config,
    bool is_tls,
    const RouteContext& context);

} // namespace http
} // namespace url_shortener
