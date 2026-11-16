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

/**
 * @brief Handles `GET /api/v1/commands`: the descriptor catalog for HTTP
 * clients (milestone 0006, Task 04.0 subtask 10).
 *
 * A resource-style route (like `GET /api/v1/links`, not the envelope-in/
 * envelope-out `POST /api/v1/commands`): on success the body is the `result`
 * object of the built-in `command.list` command
 * (`{"commands":[{name,summary,perm,kind,mutating,dry_run,args:[...]}, ...]}`,
 * sorted by command name), and failures use the legacy
 * `{"error":{code,message,request_id}}` shape produced by the bridge.
 *
 * Pipeline (every step either answers and stops, or falls through):
 * 1. **Gate (threat model T-64):** when the command API is disabled
 *    (`command_api_enabled(config) == false`, the secure default) it answers
 *    byte-for-byte like `GET /api/v1/commands` does *before* this route exists —
 *    i.e. the router's method-mismatch reply for the POST-only path,
 *    `400 invalid_method "Only POST is supported"` — not a `404`, because
 *    `POST /api/v1/commands` (subtask 09) already makes the path exist (the same
 *    subtlety `handleListLinks` handles for `GET /api/v1/links`). The catalog is
 *    never disclosed on this branch and the query is never read.
 * 2. **Query parse (threat model T-73):** `context.query_string` is parsed by
 *    @ref parse_query_string (percent-decoded, duplicates preserved); a
 *    malformed query yields `400 invalid_request` with a fixed, value-free
 *    message. Every decoded parameter is forwarded as an `args` member (names
 *    validated against the catalog grammar first; a duplicate is a `400`), so
 *    the registry rejects it as `<name>: unknown` — `command.list` takes no
 *    arguments, so any parameter is an error.
 * 3. **Execute:** `command.list` runs through @ref execute_resource_command as
 *    the gated caller (@ref command_api_caller). `command.list` is a `read`
 *    command, so a `User` caller may list; a permission failure maps to
 *    `403 not_authorized` via @ref resource_response.
 * 4. **Render:** @ref resource_response serializes the outcome — the
 *    `command.list` result verbatim at `200`, or the legacy error envelope.
 *
 * The response never echoes the raw query (contract C6). The catalog exposes
 * only each descriptor's name/summary/perm/kind/mutating/dry_run and argument
 * schema — never a handler, `maps_to`, secret or path (threat model T-74). The
 * redirect fast path is untouched (contract C10): this is the management plane.
 *
 * @param req Incoming request; supplies the HTTP version and the request id
 * echoed into the response.
 * @param config Server configuration; `command_api_enabled`/`command_api_role`
 * select the gate and caller role.
 * @param is_tls `true` on HTTPS connections; gates HSTS/`nosniff`.
 * @param context Route context; `context.query_string` is parsed for `args`
 * and `context.peer` is the gated caller's locality input (never a header,
 * never logged, threat model T-70/T-84).
 * @return `400 invalid_method` (gate off, byte-equal to the pre-existing
 * method-mismatch reply); `400 invalid_request` (malformed query or an unknown/
 * duplicate/invalid parameter); otherwise a `200` whose body is the
 * `command.list` result, or a legacy error envelope at the shared error-table
 * status.
 */
BeastResponse handleListCommands(const BeastRequest& req,
                                 const ::ServerConfig& config,
                                 bool is_tls,
                                 const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
