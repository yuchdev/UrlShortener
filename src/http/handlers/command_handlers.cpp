#include <cctype>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/args_builder.hpp>
#include <url_shortener/commands/command_registry.hpp>
#include <url_shortener/commands/envelope.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/http/command_bridge.hpp>
#include <url_shortener/http/handlers/command_handlers.hpp>
#include <url_shortener/http/query_params.hpp>
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
    const auto not_space = [](const unsigned char c)
    { return std::isspace(c) == 0; };
    while (!header.empty()
           && !not_space(static_cast<unsigned char>(header.front())))
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

/**
 * @brief Whether a query-parameter name matches the args-object name grammar
 * `[a-z][a-z0-9_]*`.
 *
 * @ref commands::args_builder::add_string throws `std::invalid_argument` on a
 * name outside this grammar, so @ref handleListCommands validates every
 * forwarded name here first and turns a bad name into a `400` rather than
 * letting it reach (and throw inside) the builder. A name that matches the
 * grammar but is not a `command.list` argument (which takes none) is still
 * forwarded, so the registry reports it as `<name>: unknown`. Mirrors the
 * identically-named helper in `link_handlers.cpp` (duplicated rather than
 * shared to keep the handler files decoupled in this slice).
 *
 * @param name Decoded query-parameter name.
 * @return `true` when @p name is non-empty, starts with `a-z`, and otherwise
 * contains only `a-z`, `0-9` or `_`; `false` otherwise.
 */
bool is_valid_arg_name(const std::string& name)
{
    if (name.empty() || name.front() < 'a' || name.front() > 'z') {
        return false;
    }
    for (const char c : name) {
        const bool ok =
            (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Canonicalizes an all-digits query value into a JSON integer token, or
 * reports that it is not a plain integer.
 *
 * `job.list`'s integer arguments (`offset`, `limit`) are emitted as JSON number
 * tokens via `args_builder::add_raw`, which inserts the token verbatim and
 * never validates it. To keep that token always valid JSON (JSON forbids
 * leading zeros and there is no octal/ambiguous parse), this accepts only a
 * `^[0-9]+$` value of at most 18 digits (so it always fits `long long`) and
 * strips its leading zeros, keeping a single `0` for an all-zero input. So
 * `007` -> `7`, `00` -> `0` (interpreted as decimal, never octal). Anything
 * else — a sign, non-digit, empty, or 19+ digits — returns `std::nullopt`, and
 * the caller forwards the raw value as a JSON **string** instead, so the
 * registry rejects it as
 * `<arg>: wrong_type`. Mirrors the identically-named helper in
 * `link_handlers.cpp` (duplicated rather than shared to keep the handler files
 * decoupled in this slice).
 *
 * @param value Decoded parameter value.
 * @return The canonical decimal integer token (no leading zeros) when @p value
 * is 1..18 ASCII digits; `std::nullopt` otherwise.
 */
std::optional<std::string> canonical_integer_token(const std::string& value)
{
    if (value.empty() || value.size() > 18) {
        return std::nullopt;
    }
    for (const char c : value) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
    }
    const std::size_t first = value.find_first_not_of('0');
    if (first == std::string::npos) {
        return std::string("0");
    }
    return value.substr(first);
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
    // not parsed on a refusal. Beast's string_view is converted by
    // data()/size() (it is not implicitly convertible to std::string_view).
    const auto content_type = req[bhttp::field::content_type];
    if (!is_application_json(
            std::string_view(content_type.data(), content_type.size())))
    {
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
    // max_depth 8) so a deep/overlong body is still rejected by the scanner.
    // The member cap (256) and key-length cap (128) live in the scanner's own
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

    // 5. Create throttle (threat model T-75): link.create - including a
    // dry_run, which runs the full SSRF/validator and is not cheaper - consumes
    // the same per-peer throttle as POST /api/v1/links, before execute. No
    // other command is throttled here.
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

BeastResponse handleListCommands(const BeastRequest& req,
                                 const ::ServerConfig& config,
                                 const bool is_tls,
                                 const RouteContext& context)
{
    // 1. Gate (threat model T-64): when the command API is disabled (the secure
    // default), answer byte-for-byte like GET /api/v1/commands did before this
    // route existed. POST /api/v1/commands (subtask 09) already makes the path
    // exist, so today's router returns 400 invalid_method "Only POST is
    // supported" (the same subtlety handleListLinks handles for the POST-only
    // /api/v1/links path), not a 404. The catalog is never disclosed here and
    // the query is never read.
    if (!command_api_enabled(config)) {
        return url_shortener::makeApiErrorResponse(req,
                                                   config,
                                                   is_tls,
                                                   400,
                                                   "invalid_method",
                                                   "Only POST is supported");
    }

    // 2. Parse the query string (percent-decoded, repeated keys preserved). A
    // malformed query -> 400 invalid_request with a fixed, value-free message
    // (threat model T-73). command.list takes no arguments.
    std::vector<query_param> params;
    std::string parse_error;
    if (!parse_query_string(context.query_string, &params, &parse_error)) {
        return url_shortener::makeApiErrorResponse(req,
                                                   config,
                                                   is_tls,
                                                   400,
                                                   "invalid_request",
                                                   "malformed query string");
    }

    // Forward every parameter as an args member (contract C1: the handler only
    // assembles args via args_builder). Each name is validated against the
    // args-object grammar first (the builder throws on a bad name) and a
    // duplicate is a 400; a grammar-valid but unknown name is forwarded so the
    // registry rejects it as <name>: unknown (surfaced as 400 invalid_request).
    commands::args_builder args;
    for (const query_param& param : params) {
        if (!is_valid_arg_name(param.name)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_request",
                "invalid parameter name");
        }
        if (args.has(param.name)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_request",
                param.name + ": duplicate");
        }
        args.add_string(param.name, param.value);
    }

    // 3/4. Execute command.list as the gated caller (subtask 08) and render
    // resource-style: the result verbatim at 200, or the legacy error envelope
    // (a permission failure maps to 403 not_authorized). command.list is a read
    // command, so a User caller may list (threat model T-74).
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(config,
                                 "command.list",
                                 args.str(),
                                 requestIdFromRequest(req),
                                 command_api_caller(config, context.peer)),
        200);
}

BeastResponse handleListJobs(const BeastRequest& req,
                             const ::ServerConfig& config,
                             const bool is_tls,
                             const RouteContext& context)
{
    // 1. Gate (threat model T-64): when the command API is disabled (the secure
    // default), answer byte-for-byte like an unmatched /api/v1 route - 404
    // not_found "Route not found", the router's own not-found reply (and what
    // the /{path} fallback returns for a reserved /api/ path). The runner is
    // never read and the query is never parsed on this branch.
    if (!command_api_enabled(config)) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 404, "not_found", "Route not found");
    }

    // 2. Parse the query string (percent-decoded, repeated keys preserved). A
    // malformed query -> 400 invalid_request with a fixed, value-free message
    // (threat model T-73).
    std::vector<query_param> params;
    std::string parse_error;
    if (!parse_query_string(context.query_string, &params, &parse_error)) {
        return url_shortener::makeApiErrorResponse(req,
                                                   config,
                                                   is_tls,
                                                   400,
                                                   "invalid_request",
                                                   "malformed query string");
    }

    // Build the job.list args from the query (contract C1: the handler only
    // assembles args via args_builder; the filter/paging semantics live in
    // job.list). Each name is validated against the args grammar first (the
    // builder throws on a bad name) and a duplicate is a 400. offset/limit that
    // are plain base-10 integers are emitted as JSON number tokens; everything
    // else - including unknown names and a bad state - is a string, so the
    // registry rejects it (<name>: unknown / <arg>: wrong_type /
    // state: not_in_enum / out-of-range bound) as 400 invalid_request.
    commands::args_builder args;
    for (const query_param& param : params) {
        if (!is_valid_arg_name(param.name)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_request",
                "invalid parameter name");
        }
        if (args.has(param.name)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_request",
                param.name + ": duplicate");
        }
        if (param.name == "offset" || param.name == "limit") {
            if (const auto token = canonical_integer_token(param.value)) {
                args.add_raw(param.name, *token);
                continue;
            }
        }
        args.add_string(param.name, param.value);
    }

    // 3/4. Execute job.list as the gated caller (subtask 08) and render
    // resource-style: the result verbatim at 200 (the result-free summary page,
    // threat model T-109), or the legacy error envelope. job.list is a read
    // command, so a User caller may list (threat model T-92: jobs carry no
    // owner in V1; ids are correlation handles only, T-93).
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(config,
                                 "job.list",
                                 args.str(),
                                 requestIdFromRequest(req),
                                 command_api_caller(config, context.peer)),
        200);
}

BeastResponse handleGetJob(const BeastRequest& req,
                           const ::ServerConfig& config,
                           const bool is_tls,
                           const RouteContext& context)
{
    // 1. Gate (threat model T-64): gate off answers byte-for-byte like an
    // unmatched /api/v1 route (404 not_found "Route not found"); the runner
    // is never read.
    if (!command_api_enabled(config)) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 404, "not_found", "Route not found");
    }

    // 2/3. Forward the path id as the sole job.get arg and execute as the gated
    // caller (subtask 08). job.get is a read command, so a User caller may read
    // (threat model T-92/T-93). resource_response renders the outcome: the full
    // job object verbatim at 200 (a cancelled job reads 200 with a nested
    // error.code job_cancelled, catalog error table), an unknown id as 404
    // not_found, the raw path id is never echoed (contract C6).
    commands::args_builder args;
    args.add_string("id", pathParam(context, "id").value_or(std::string {}));
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(config,
                                 "job.get",
                                 args.str(),
                                 requestIdFromRequest(req),
                                 command_api_caller(config, context.peer)),
        200);
}

BeastResponse handleCancelJob(const BeastRequest& req,
                              const ::ServerConfig& config,
                              const bool is_tls,
                              const RouteContext& context)
{
    // 1. Gate (threat model T-64): gate off answers byte-for-byte like an
    // unmatched /api/v1 route (404 not_found "Route not found"); the runner
    // is never touched. Checked FIRST, before the content type, so a gated-off
    // route is indistinguishable from an unmatched one (no 415 oracle).
    if (!command_api_enabled(config)) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 404, "not_found", "Route not found");
    }

    // 2. Content-Type must be application/json (CSRF defence, threat model
    // T-67/CWE-352). job.cancel is a state-changing write a loopback admin runs,
    // so without this a cross-origin HTML form POST (which can only send
    // text/plain, application/x-www-form-urlencoded or multipart/form-data)
    // could cancel every job by sequential id from the admin's browser.
    // Requiring application/json forces a CORS preflight the server never
    // grants, exactly as POST /api/v1/commands does. The body itself is never
    // read (an empty body or `{}` is accepted); only the media type is checked
    // (parameters ignored, case-insensitive). Beast's string_view is converted
    // via data()/size() (it is not implicitly convertible to std::string_view).
    const auto content_type = req[bhttp::field::content_type];
    if (!is_application_json(
            std::string_view(content_type.data(), content_type.size())))
    {
        return url_shortener::makeApiErrorResponse(
            req,
            config,
            is_tls,
            415,
            "unsupported_media_type",
            "Content-Type must be application/json");
    }

    // 3/4. Forward the path id as the sole job.cancel arg and execute as the
    // gated caller (subtask 08). job.cancel is a write/mutating command, so
    // execute denies a User caller not_authorized before the handler (contract
    // C7, threat model T-92): cancel runs only as a loopback admin, and a User
    // or a non-loopback/absent peer under role admin gets 403 with the job left
    // running. The resource-route Admin caller is never used here (decision
    // D4). resource_response maps not_found -> 404 and not_authorized -> 403.
    commands::args_builder args;
    args.add_string("id", pathParam(context, "id").value_or(std::string {}));
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(config,
                                 "job.cancel",
                                 args.str(),
                                 requestIdFromRequest(req),
                                 command_api_caller(config, context.peer)),
        200);
}

}  // namespace http
}  // namespace url_shortener
