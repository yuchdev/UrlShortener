#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/command_bridge.hpp>
#include <url_shortener/http/handlers/command_handlers.hpp>
#include <url_shortener/http/request_handlers.hpp>

namespace url_shortener
{
namespace http
{
namespace
{

namespace bhttp = boost::beast::http;

/**
 * @brief Whether a `Content-Type` header value selects `application/json`.
 *
 * Compares only the media type: it drops any `;`-separated parameters (so
 * `application/json; charset=utf-8` matches), trims surrounding whitespace, and
 * matches case-insensitively (so `Application/JSON` matches). Requiring this
 * exact media type forces a CORS preflight the server never grants, blocking
 * drive-by cross-origin form POSTs (threat model T-67).
 *
 * @param header The raw `Content-Type` header value (possibly empty when the
 * header is absent).
 * @return `true` when the media type is exactly `application/json`; `false` for
 * a missing header, `text/plain`, `application/x-www-form-urlencoded`, or any
 * other type.
 */
bool is_application_json(std::string_view header)
{
    const std::size_t semicolon = header.find(';');
    if (semicolon != std::string_view::npos) {
        header = header.substr(0, semicolon);
    }
    const auto not_space = [](const unsigned char c) {
        return std::isspace(c) == 0;
    };
    while (!header.empty() && !not_space(static_cast<unsigned char>(header.front())))
    {
        header.remove_prefix(1);
    }
    while (!header.empty()
           && !not_space(static_cast<unsigned char>(header.back())))
    {
        header.remove_suffix(1);
    }
    static constexpr std::string_view k_json = "application/json";
    if (header.size() != k_json.size()) {
        return false;
    }
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(header[i])) != k_json[i]) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Renders a command response as the HTTP response the generic endpoint
 * returns: the serialized envelope at the shared error-table status.
 *
 * @param req Incoming request; mirrored into the response headers.
 * @param config Server configuration passed to the response helper.
 * @param is_tls `true` on HTTPS connections.
 * @param response The command outcome to serialize.
 * @return A `Content-Type: application/json` response whose body is
 * @ref commands::serialize_response(@p response) and whose status is
 * @ref commands::http_status_for(@p response).
 */
BeastResponse renderEnvelope(const BeastRequest& req,
                             const ::ServerConfig& config,
                             const bool is_tls,
                             const commands::CommandResponse& response)
{
    return url_shortener::makeResponse(req,
                                       config,
                                       is_tls,
                                       commands::http_status_for(response),
                                       commands::serialize_response(response),
                                       "application/json");
}

}  // namespace

BeastResponse handleExecuteCommand(const BeastRequest& req,
                                   const ::ServerConfig& config,
                                   const bool is_tls,
                                   const RouteContext& context)
{
    // 1. Gate (threat model T-64): disabled -> answer exactly like an unmatched
    // route (404 not_found "Route not found"), byte-identical to the router's
    // own not-found reply, and never parse the body.
    if (!command_api_enabled(config)) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 404, "not_found", "Route not found");
    }

    // 2. Content-Type must be application/json (threat model T-67). The body is
    // not parsed on a refusal. Beast's string_view is converted by data()/size()
    // (it is not implicitly convertible to std::string_view).
    const auto content_type = req[bhttp::field::content_type];
    if (!is_application_json(
            std::string_view(content_type.data(), content_type.size()))) {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            415,
            "unsupported_media_type",
            "Content-Type must be application/json");
    }

    // 3. Full envelope limits (threat model T-68): override only max_bytes and
    // max_id_len from config; keep the hardening defaults (max_command_len 64,
    // max_depth 8) so a deep/overlong body is still rejected by the scanner. The
    // member cap (256) and key-length cap (128) live in the scanner's own
    // scan_limits inside parse_request and must not be loosened.
    commands::envelope_limits limits;
    limits.max_bytes = config.max_request_body_bytes;
    limits.max_id_len = config.request_id_max_length;

    // 4. Parse: the body is parsed by nothing but parse_request. On failure,
    // render that invalid_envelope at its table status; the raw body is never
    // echoed (contract C6).
    const commands::parse_request_result parsed =
        commands::parse_request(req.body(), limits);
    if (!parsed.ok()) {
        return renderEnvelope(req,
                              config,
                              is_tls,
                              commands::make_failure(parsed.id, parsed.error));
    }
    const commands::CommandRequest& request = *parsed.request;

    // 5. Create throttle (threat model T-75): link.create - including a dry_run,
    // which runs the full SSRF/validator and is not cheaper - consumes the same
    // per-peer throttle as POST /api/v1/links, before execute. No other command
    // is throttled here.
    if (request.command == "link.create") {
        if (std::optional<BeastResponse> throttled =
                apply_create_throttle(req, config, is_tls, context))
        {
            return std::move(*throttled);
        }
    }

    // 6. Execute as the gated caller (threat model T-64/T-70): Admin only for a
    // loopback peer under role admin; absent/remote peer runs as User.
    // execute_command_envelope preserves request.options (so a dry_run on a
    // non-dry-run command surfaces as unsupported) and refuses manage_users
    // (threat model T-61) before execute.
    return renderEnvelope(
        req,
        config,
        is_tls,
        execute_command_envelope(
            config, request, command_api_caller(config, context.peer)));
}

}  // namespace http
}  // namespace url_shortener
