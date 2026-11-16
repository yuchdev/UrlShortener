#pragma once

#include <url_shortener/http/handler_types.hpp>

namespace url_shortener
{
namespace http
{

/**
 * @brief Handles `POST /api/v1/commands`: the generic command transport entry
 * point of ADR 0002 (milestone 0006, Task 04.0 subtask 09).
 *
 * Takes one request envelope as the body and returns one response envelope,
 * with the HTTP status taken from the shared error table
 * (@ref commands::http_status_for). It is the only way HTTP clients reach
 * commands that have no dedicated resource route.
 *
 * Pipeline (every step either answers and stops, or falls through):
 * 1. **Gate (threat model T-64):** when the command API is disabled
 *    (`command_api_enabled(config) == false`, the secure default) it answers
 *    exactly like an unmatched route — `404 not_found "Route not found"`,
 *    byte-identical to the router's own not-found reply — and the body is
 *    **not** parsed.
 * 2. **Content type (threat model T-67):** the request's `Content-Type` media
 *    type (parameters ignored, case-insensitive) must be `application/json`,
 *    else `415 unsupported_media_type`; the body is not parsed. This forces a
 *    CORS preflight the server never grants, blocking drive-by cross-origin
 *    form POSTs.
 * 3. **Parse (threat model T-68):** the body is parsed only by
 *    @ref commands::parse_request, under a full @ref commands::envelope_limits
 *    whose hardening defaults (`max_depth`, `max_command_len`) survive and only
 *    `max_bytes`/`max_id_len` are overridden from `config`. A parse failure is
 *    rendered as its `invalid_envelope` envelope at its table status.
 * 4. **Create throttle (threat model T-75):** a `link.create` request —
 *    including one carrying `options.dry_run` — consumes the same per-peer
 *    create throttle as `POST /api/v1/links` via @ref apply_create_throttle,
 *    before execution; over limit yields `429 rate_limited`.
 * 5. **Execute:** @ref execute_command_envelope runs the parsed request
 *    (preserving `options`) as the gated caller
 *    (@ref command_api_caller, `Admin` only for a loopback peer under role
 *    `admin`), refusing `manage_users` first (threat model T-61).
 * 6. **Render:** the response envelope is serialized and returned with
 *    `Content-Type: application/json` at @ref commands::http_status_for.
 *
 * The response is always the serialized envelope (never the legacy
 * `{"error":{...}}` shape) and never echoes the raw request body (contract C6).
 * The envelope `id` and the HTTP `X-Request-Id` header are independent. The
 * redirect fast path is untouched (contract C10): this is the management plane.
 *
 * @param req Incoming request; supplies the HTTP version, the body, the
 * `Content-Type` header, and the request id echoed into the HTTP headers.
 * @param config Server configuration; `command_api_enabled`/`command_api_role`
 * select the gate and caller role, and `max_request_body_bytes`/
 * `request_id_max_length` bound the envelope parse.
 * @param is_tls `true` on HTTPS connections; gates HSTS/`nosniff`.
 * @param context Route context; only `context.peer` is read, as the gated
 * caller's locality input and the create-throttle key (never a header; never
 * logged, threat model T-70/T-84).
 * @return `404 not_found` (gate off, byte-equal to an unmatched route); `415
 * unsupported_media_type` (wrong/missing content type); `429 rate_limited`
 * (`link.create` over the per-peer limit); otherwise a `200`/error response
 * whose body is the serialized command envelope and whose status is the shared
 * error-table status for the command outcome.
 */
BeastResponse handleExecuteCommand(const BeastRequest& req,
                                   const ::ServerConfig& config,
                                   bool is_tls,
                                   const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
