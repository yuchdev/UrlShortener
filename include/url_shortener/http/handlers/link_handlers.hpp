#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener
{
namespace http
{

/// @brief Selects which JSON field may carry a caller-supplied short code
/// when creating a link.
enum class CreateSlugFieldMode
{
    ///< Only the `slug` field is accepted as a caller-supplied short code.
    slug_only,

    ///< Either `slug` or the legacy `code` field may supply the short
    ///< code; `slug` takes precedence when both are present.
    slug_or_code
};

/// @brief Parses the request body JSON, applies the create throttle, and
/// creates a new short link through the `link.create` command registry,
/// accepting `slug` and optionally `code` as the short-code field depending on
/// `slug_mode`.
///
/// Shared by both the canonical (`handleCreateLink`) and compatibility
/// (`handleCompatCreateLink`) create endpoints. The per-field 400 shape
/// validation and its order are the public REST contract and run unchanged;
/// only the DTO construction and service call are replaced by an `args_builder`
/// plus the command bridge (milestone 0006 contract C1/C3). After the per-field
/// checks and before building/executing the command, the create throttle
/// (threat model T-75) is applied: when a limiter is wired, `context.peer` is
/// present, and `config.link_create_rate_limit > 0`, an over-limit request
/// returns `429 rate_limited` without creating anything. A limiter failure is
/// fail-open (the create proceeds); an absent peer skips the throttle.
/// @param req       Incoming request; body must be JSON with at least a
///        `url` (or `target_url`) field.
/// @param config    Server-wide runtime configuration; supplies the create
///        throttle limit/window.
/// @param is_tls    `true` on HTTPS connections.
/// @param slug_mode Controls whether the legacy `code` field is accepted
///        in addition to `slug`.
/// @param context   Route context; only `context.peer` is read, as the create
///        throttle key (never a header; never logged, threat model T-70/T-75).
/// @return 201 with the JSON link view on success; 400 for missing/invalid
///         fields (url, slug, redirect_type, expires_at, enabled, tags,
///         metadata, campaign); 409 if the slug is reserved or already
///         in use; 429 `rate_limited` when the per-peer create limit is
///         exceeded; 500 on repository failure.
BeastResponse createLinkFromRequest(const BeastRequest& req,
                                    const ::ServerConfig& config,
                                    bool is_tls,
                                    CreateSlugFieldMode slug_mode,
                                    const RouteContext& context);

/// @brief Handles `POST /api/v1/links`; creates a new short link.
/// @param req     Incoming request with JSON body.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; forwarded to `createLinkFromRequest` for the
///        create throttle peer key.
/// @return See `createLinkFromRequest` with `CreateSlugFieldMode::slug_only`.
BeastResponse handleCreateLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               bool is_tls,
                               const RouteContext& context);

/// @brief Handles `GET /api/v1/links`; lists links with filtering, sorting and
/// paging through the `link.list` command (milestone 0006 Task 04.0 subtask
/// 07).
///
/// The only gated REST resource route: when the command API is disabled
/// (`command_api_enabled(config) == false`, the default) it answers exactly
/// like today's unmatched `GET /api/v1/links`, so no caller reaches
/// `link.list` and no enumeration oracle exists (user decision 2026-11-16,
/// threat model T-71). When enabled it runs `link.list` as the gated caller
/// (`command_api_caller(config, context.peer)`): a `User` caller may list only
/// non-deleted links, while `status=all`/`status=deleted` require write/admin
/// and are rejected with `403 not_authorized` for a `User` (enforced by the
/// `link.list` descriptor's permission hook, not here). The query string is the
/// command's `args`: `status`, `q`, `tag`, `campaign`, `sort`, `order`,
/// `offset`, `limit`. Hostile input (malformed `%`, duplicate names, unknown
/// names, out-of-range or wrong-typed values) yields `400 invalid_request`,
/// never a crash or 500. The redirect fast path is untouched (contract C10).
/// @param req     Incoming request; supplies the HTTP version and request id.
/// @param config  Server-wide runtime configuration; `command_api_enabled` and
///        `command_api_role` select the gate behaviour and caller role.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `query_string` carries the filters and `peer`
///        is the gate's locality input (loopback -> admin honoured).
/// @return 405 `invalid_method` with `Allow: POST` (gate off, mirroring the
///         router's method-mismatch reply for the POST-only path, RFC 9110
///         15.5.6);
///         200 with the `{"items":[...],"offset":N,"limit":N,"total":N}` page
///         on success; 400 `invalid_request` for a malformed query or a bad
///         parameter; 403 `not_authorized` when a non-admin caller requests
///         `status=all`/`deleted`.
BeastResponse handleListLinks(const BeastRequest& req,
                              const ::ServerConfig& config,
                              bool is_tls,
                              const RouteContext& context);

/// @brief Handles `GET /api/v1/links/id/{id}`; retrieves a link by its
/// stable opaque identifier.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `id` path parameter identifies the link.
/// @return 200 with the full JSON link view on success; 404 if no link
///         with that ID exists.
BeastResponse handleGetLinkById(const BeastRequest& req,
                                const ::ServerConfig& config,
                                bool is_tls,
                                const RouteContext& context);

/// @brief Handles `GET /api/v1/links/{slug}`; retrieves a link by its
/// short-code slug.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link.
/// @return 200 with the full JSON link view on success; 404 if the slug
///         does not exist.
BeastResponse handleGetLinkBySlug(const BeastRequest& req,
                                  const ::ServerConfig& config,
                                  bool is_tls,
                                  const RouteContext& context);

/// @brief Handles `PATCH /api/v1/links/{slug}`; applies a partial update
/// (PATCH semantics) to an existing link.
///
/// Performs a 404-before-validation check: if the slug is not found the
/// handler returns 404 without evaluating the request body.  Absent JSON
/// fields are left untouched; explicit JSON `null` clears nullable fields
/// (`expires_at`, `campaign`).
/// @param req     Incoming request; body must be JSON with the fields to
///        update.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link to patch.
/// @return 200 with the updated JSON link view on success; 400 for
///         invalid field values; 404 if the slug does not exist; 500 on
///         repository failure.
BeastResponse handlePatchLink(const BeastRequest& req,
                              const ::ServerConfig& config,
                              bool is_tls,
                              const RouteContext& context);

/// @brief Handles `DELETE /api/v1/links/{slug}`; soft-deletes a link by
/// setting its `deleted_at` timestamp.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link to soft-delete.
/// @return 200 with the post-delete JSON link view on success; 404 if
///         the slug does not exist; 500 on repository failure.
BeastResponse handleDeleteLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               bool is_tls,
                               const RouteContext& context);

/// @brief Handles `GET /api/v1/links/{slug}/preview`; returns the
/// reduced preview projection of a link.
///
/// Emits only `slug`, `url`, `status`, `redirect_type`, `enabled`,
/// `expires_at`, and `deleted_at`.  For the full link view use
/// `handleGetLinkBySlug`.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link.
/// @return 200 with the reduced preview JSON on success; 404 if the
///         slug does not exist.
BeastResponse handlePreviewLink(const BeastRequest& req,
                                const ::ServerConfig& config,
                                bool is_tls,
                                const RouteContext& context);

/// @brief Handles `GET /api/v1/links/{slug}/qr` and
/// `/api/v1/links/{slug}/routing`; stub for unimplemented features.
///
/// Verifies the slug exists before returning 501, so callers receive
/// 404 for unknown slugs and 501 for known slugs with deferred features.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter.
/// @return 404 if the slug does not exist; 501 `feature_not_enabled`
///         otherwise.
BeastResponse handlePlaceholderFeature(const BeastRequest& req,
                                       const ::ServerConfig& config,
                                       bool is_tls,
                                       const RouteContext& context);

/// @brief Handles `GET /api/v1/links/{slug}/stats`; returns redirect
/// analytics for a link.
///
/// When `from`, `to`, and `bucket` query parameters are all present,
/// delegates to `LinkCommandService::GetLinkStats` for aggregate stats.
/// Otherwise falls back to the inline counters stored in the link record
/// (`total_redirects`, `redirects_24h`, `redirects_7d`,
/// `last_accessed_at`).
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter and optional
///        query parameters (`from`, `to`, `bucket`) control the shape.
/// @return 200 with the JSON stats on success; 404 if the slug does not
///         exist; 400 if query parameters are present but invalid.
BeastResponse handleLinkStats(const BeastRequest& req,
                              const ::ServerConfig& config,
                              bool is_tls,
                              const RouteContext& context);

/// @brief Handles `POST /api/v1/links/{slug}/enable`; sets the link's
/// `enabled` flag to `true`.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link.
/// @return 200 with the updated JSON link view on success; 404 if the
///         slug does not exist; 500 on repository failure.
BeastResponse handleEnableLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               bool is_tls,
                               const RouteContext& context);

/// @brief Handles `POST /api/v1/links/{slug}/disable`; sets the link's
/// `enabled` flag to `false`.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link.
/// @return 200 with the updated JSON link view on success; 404 if the
///         slug does not exist; 500 on repository failure.
BeastResponse handleDisableLink(const BeastRequest& req,
                                const ::ServerConfig& config,
                                bool is_tls,
                                const RouteContext& context);

/// @brief Handles `POST /api/v1/links/{slug}/restore`; clears
/// `deleted_at` on a soft-deleted link.
/// @param req     Incoming request.
/// @param config  Server-wide runtime configuration.
/// @param is_tls  `true` on HTTPS connections.
/// @param context Route context; `slug` path parameter identifies the
///        link to restore.
/// @return 200 with the updated JSON link view on success; 404 if the
///         slug does not exist; 500 on repository failure.
BeastResponse handleRestoreLink(const BeastRequest& req,
                                const ::ServerConfig& config,
                                bool is_tls,
                                const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
