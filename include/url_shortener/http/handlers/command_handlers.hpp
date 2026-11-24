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
 *    like the router's method-mismatch reply for the POST-only path — `405
 * Method Not Allowed` with `Allow: POST` (RFC 9110 15.5.6) and the unchanged
 * `invalid_method "Only POST is supported"` JSON body — not a `404`, because
 * `POST /api/v1/commands` (subtask 09) already makes the path exist (the same
 *    subtlety `handleListLinks` handles for `GET /api/v1/links`). The catalog
 * is never disclosed on this branch and the query is never read.
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
 * @return `405 invalid_method` with `Allow: POST` (gate off, mirroring the
 * router's method-mismatch reply for the POST-only path); `400 invalid_request`
 * (malformed query or an unknown/
 * duplicate/invalid parameter); otherwise a `200` whose body is the
 * `command.list` result, or a legacy error envelope at the shared error-table
 * status.
 */
BeastResponse handleListCommands(const BeastRequest& req,
                                 const ::ServerConfig& config,
                                 bool is_tls,
                                 const RouteContext& context);

/**
 * @brief Handles `GET /api/v1/jobs`: the paged job listing (milestone 0006,
 * Task 05.0 subtask 11), a thin projection onto the `job.list` command.
 *
 * A resource-style route (like `GET /api/v1/links`): on success the body is the
 * `result` object of `job.list` (`{"items":[<job summary>...],"total":n}`,
 * each item `result`-free per the 2026-11-18 decision / threat model T-109);
 * failures use the legacy `{"error":{...}}` shape produced by the bridge.
 *
 * Pipeline (every step either answers and stops, or falls through):
 * 1. **Gate (threat model T-64):** when the command API is disabled
 *    (`command_api_enabled(config) == false`, the secure default) it answers
 *    byte-for-byte like an unmatched `/api/v1` route — `404 not_found
 *    "Route not found"` — and the job runner is never read.
 * 2. **Query parse (threat model T-73):** `context.query_string` is parsed by
 *    @ref parse_query_string (percent-decoded, duplicates preserved); a
 *    malformed query yields `400 invalid_request` with a fixed, value-free
 *    message. Every decoded parameter is forwarded as a `job.list` arg (names
 *    validated against the args grammar first; a duplicate is a `400`);
 *    `offset`/`limit` that are plain base-10 integers are emitted as JSON
 *    number tokens, everything else — including unknown names and a bad `state`
 *    — is a string, so the registry rejects it (`<name>: unknown`,
 *    `<arg>: wrong_type`, `state: not_in_enum`, or an out-of-range bound) as
 *    `400 invalid_request`.
 * 3. **Execute:** `job.list` runs through @ref execute_resource_command as the
 *    gated caller (@ref command_api_caller). It is a `read` command, so a
 *    `User` caller may list (threat model T-92: jobs carry no owner in V1, so
 *    any authorized caller sees any job; ids are correlation handles only,
 *    T-93).
 * 4. **Render:** @ref resource_response serializes the outcome — the `job.list`
 *    result verbatim at `200`, or the legacy error envelope.
 *
 * The response never echoes the raw query (contract C6). The redirect fast path
 * is untouched (contract C10): this is the management plane.
 *
 * @param req Incoming request; supplies the HTTP version and the request id
 * echoed into the response.
 * @param config Server configuration; `command_api_enabled`/`command_api_role`
 * select the gate and caller role.
 * @param is_tls `true` on HTTPS connections; gates HSTS/`nosniff`.
 * @param context Route context; `context.query_string` is parsed for args and
 * `context.peer` is the gated caller's locality input (never a header, never
 * logged, threat model T-70/T-84).
 * @return `404 not_found` (gate off, byte-equal to an unmatched route);
 * `400 invalid_request` (malformed query, an unknown/duplicate/invalid
 * parameter, a bad `state`, or an out-of-range `limit`/`offset`); otherwise a
 * `200` whose body is the `job.list` result.
 */
BeastResponse handleListJobs(const BeastRequest& req,
                             const ::ServerConfig& config,
                             bool is_tls,
                             const RouteContext& context);

/**
 * @brief Handles `GET /api/v1/jobs/{id}`: one job record (milestone 0006,
 * Task 05.0 subtask 11), a thin projection onto the `job.get` command.
 *
 * A resource-style route: on success the body is the `result` object of
 * `job.get` (the full catalog job object, `result` included when present); a
 * cancelled job reads `200` with `error.code == "job_cancelled"` nested inside
 * that object (catalog error table). Failures use the legacy `{"error":{...}}`
 * shape produced by the bridge.
 *
 * Pipeline: **Gate (T-64)** — gate off answers `404 not_found "Route not
 * found"`, byte-equal to an unmatched `/api/v1` route, and the runner is
 * never read; otherwise the path `id` is forwarded as the sole `job.get` arg
 * and the command runs through @ref execute_resource_command as the gated
 * caller (@ref command_api_caller), a `read` command any role may call (threat
 * model T-92/T-93). @ref resource_response renders the outcome.
 *
 * @param req Incoming request; supplies the HTTP version and the request id.
 * @param config Server configuration; selects the gate and caller role.
 * @param is_tls `true` on HTTPS connections; gates HSTS/`nosniff`.
 * @param context Route context; `path_params["id"]` is the addressed job id and
 * `context.peer` is the gated caller's locality input (never logged,
 * T-70/T-84).
 * @return `404 not_found` (gate off, byte-equal to an unmatched route, or an
 * unknown job id); otherwise a `200` whose body is the `job.get` result.
 */
BeastResponse handleGetJob(const BeastRequest& req,
                           const ::ServerConfig& config,
                           bool is_tls,
                           const RouteContext& context);

/**
 * @brief Handles `POST /api/v1/jobs/{id}/cancel`: request cancellation of one
 * job (milestone 0006, Task 05.0 subtask 11), a thin projection onto the
 * `job.cancel` command.
 *
 * A resource-style route: on success the body is the `result` object of
 * `job.cancel` (the affected job record); failures use the legacy
 * `{"error":{...}}` shape produced by the bridge. The request body is never
 * read (an empty body or `{}` is accepted), but the `Content-Type` **is**
 * required to be `application/json` (see the pipeline).
 *
 * Pipeline (every step either answers and stops, or falls through):
 * 1. **Gate (T-64):** gate off answers `404 not_found "Route not found"`,
 *    byte-equal to an unmatched `/api/v1` route, and the runner is never read.
 *    Checked first, before the content type, so a gated-off route is
 *    indistinguishable from an unmatched one (no `415` oracle).
 * 2. **Content type (CSRF defence, threat model T-67 / CWE-352):** the media
 *    type must be `application/json` (parameters ignored, case-insensitive),
 *    else `415 unsupported_media_type` — the same shape/code
 *    `POST /api/v1/commands` returns. `job.cancel` is a state-changing write a
 *    loopback admin runs, so without this a cross-origin HTML form POST (which
 *    can only send `text/plain`, `application/x-www-form-urlencoded` or
 *    `multipart/form-data`) could cancel every job by sequential id from the
 *    admin's browser; requiring `application/json` forces a CORS preflight the
 *    server never grants. The body itself is still never read.
 * 3. **Execute:** the path `id` is forwarded as the sole `job.cancel` arg and
 *    the command runs through @ref execute_resource_command as the gated caller
 *    (@ref command_api_caller). `job.cancel` is a `write`, `mutating` command,
 *    so `execute` denies a `User` caller `not_authorized` before the handler
 *    runs (contract C7): the route cancels only as a loopback `admin` caller; a
 *    `User` or a non-loopback/absent peer under role `admin` gets a `403` and
 *    the job keeps running (threat model T-92; the resource-route `Admin`
 *    caller is never used here, decision D4).
 * 4. **Render:** @ref resource_response renders the outcome.
 *
 * @param req Incoming request; supplies the HTTP version, the request id and
 * the `Content-Type` header. Its body is never read.
 * @param config Server configuration; selects the gate and caller role.
 * @param is_tls `true` on HTTPS connections; gates HSTS/`nosniff`.
 * @param context Route context; `path_params["id"]` is the addressed job id and
 * `context.peer` is the gated caller's locality input, the sole
 * admin-escalation gate (never a header, never logged, threat model T-70/T-84).
 * @return `404 not_found` (gate off, byte-equal to an unmatched route, or an
 * unknown job id); `415 unsupported_media_type` (wrong/missing content type);
 * `403 not_authorized` (caller is not a loopback `admin`); otherwise a `200`
 * whose body is the `job.cancel` result.
 */
BeastResponse handleCancelJob(const BeastRequest& req,
                              const ::ServerConfig& config,
                              bool is_tls,
                              const RouteContext& context);

}  // namespace http
}  // namespace url_shortener
