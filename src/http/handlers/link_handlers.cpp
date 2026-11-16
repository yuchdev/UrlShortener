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
#include <url_shortener/core/utils.hpp>
#include <url_shortener/http/analytics_stats_handler.hpp>
#include <url_shortener/http/command_bridge.hpp>
#include <url_shortener/http/handlers/link_handlers.hpp>
#include <url_shortener/http/query_params.hpp>
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

/**
 * @brief Whether a query-parameter name matches the args-object name grammar
 * `[a-z][a-z0-9_]*`.
 *
 * `commands::args_builder::add_string` throws `std::invalid_argument` on a name
 * outside this grammar, so the list handler validates every forwarded name here
 * first and turns a bad name into a `400` rather than letting it reach (and
 * throw inside) the builder. A name that matches the grammar but is not a
 * `link.list` argument is still forwarded, so the registry reports it as
 * `<name>: unknown`.
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
        const bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
            || c == '_';
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
 * `link.list`'s integer arguments (`offset`, `limit`) are emitted as JSON number
 * tokens via `args_builder::add_raw`, which inserts the token verbatim and never
 * validates it. To keep that token always valid JSON (JSON forbids leading zeros
 * and there is no octal/ambiguous parse), this accepts only a `^[0-9]+$` value
 * of at most 18 digits (so it always fits `long long`) and strips its leading
 * zeros, keeping a single `0` for an all-zero input. So `007` -> `7`, `00` ->
 * `0` (interpreted as decimal, never octal). Anything else — a sign, non-digit,
 * empty, or 19+ digits — returns `std::nullopt`, and the caller forwards the raw
 * value as a JSON **string** instead, so the registry rejects it as
 * `<arg>: wrong_type`.
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

BeastResponse linkNotFound(const BeastRequest& req,
                           const ::ServerConfig& config,
                           const bool is_tls)
{
    return url_shortener::makeApiErrorResponse(
        req, config, is_tls, 404, "not_found", "Link not found");
}

/**
 * @brief Maps a lifecycle route's action word to its registry command name.
 *
 * The REST lifecycle routes (`POST /api/v1/links/{slug}/enable|disable|
 * restore`) carry their verb as a fixed `action` string; this translates that
 * closed set to the slug-only command the bridge runs: `"enable"` ->
 * `"link.enable"`,
 * `"disable"` -> `"link.disable"`, `"restore"` -> `"link.restore"`. Only those
 * three values are ever passed by the handlers below, so `"restore"` is handled
 * explicitly and the remaining enable/disable split is derived from equality
 * with `"enable"`.
 *
 * @param action The lifecycle verb from the route (`"enable"`, `"disable"`, or
 * `"restore"`); not request-controlled — it is one of the three literals the
 * handlers pass.
 * @return The dotted registry command name the bridge executes for @p action.
 */
std::string_view lifecycleCommandFor(const std::string& action)
{
    if (action == "restore") {
        return "link.restore";
    }
    return action == "enable" ? "link.enable" : "link.disable";
}

BeastResponse handleLifecycleAction(const BeastRequest& req,
                                    const ::ServerConfig& config,
                                    const bool is_tls,
                                    const RouteContext& context,
                                    const std::string& action)
{
    // Transport concern: translate the lifecycle route into its slug-only
    // registry command and run it through the command bridge as the legacy
    // Admin caller (contract C1, threat model T-60). enable/disable/restore map
    // to link.enable/link.disable/link.restore; the read-mutate-persist logic
    // (including the not_found path) lives in the command handler / link
    // command service, the single mutation path shared with the CLI adapter.
    // This handler holds no app:: command DTO nor error-code switch (contract
    // C1); statuses, bodies and error codes come from the bridge (contract C3).
    // No request body is read. The redirect fast path is unaffected (contract
    // C10): this is the management plane.
    commands::args_builder args;
    args.add_string("slug", pathValue(context, "slug"));
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(config,
                                 lifecycleCommandFor(action),
                                 args.str(),
                                 requestIdFromRequest(req)),
        200);
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
    // only (never a header). Skipped only when no limiter is wired or the limit
    // is disabled (0). An over-limit decision (!allowed AND no error) is a 429;
    // a limiter failure (error set) is fail-open and falls through to the
    // create.
    //
    // Key scope (threat model, key-namespace collision): the key carries a
    // "link_create:" prefix so it cannot collide with other IRateLimiter users
    // that share one Redis rate_limit backend (the Redis limiter composes
    // prefix+scope+":"+key, and every create-throttle user shares that scope).
    //
    // Peerless requests (remote_endpoint error, non-socket dispatch) share one
    // fixed fallback bucket ("peer:unknown") rather than bypassing the throttle
    // - fail-closed to a shared bucket. SPEC DEVIATION: the spec said a
    // peerless request is not throttled; the security review replaced that with
    // a shared fallback bucket so an attacker cannot escape the throttle by
    // suppressing the peer.
    if (const auto limiter = create_rate_limiter();
        limiter && config.link_create_rate_limit > 0)
    {
        const std::string throttle_key = "link_create:"
            + (context.peer.has_value() ? context.peer->to_string()
                                        : std::string("peer:unknown"));
        RateLimitError rl_error = RateLimitError::none;
        const RateLimitDecision decision = limiter->Allow(
            throttle_key,
            config.link_create_rate_limit,
            std::chrono::seconds(config.link_create_rate_window_seconds),
            &rl_error);
        if (!decision.allowed && rl_error == RateLimitError::none) {
            // Over limit: peer-free counter only (no log flooding, no PII).
            metricsRegistry().create_throttle_rejected_total.fetch_add(1);
            return url_shortener::makeApiErrorResponse(
                req, config, is_tls, 429, "rate_limited", "Too many requests");
        }
        if (rl_error != RateLimitError::none) {
            // Limiter failure -> fail open (Redis-down convention). Count every
            // occurrence and warn only on the first so a sustained limiter
            // outage is visible without per-request flooding. Never log the
            // peer, the key, or the body (client PII, threat model T-70/T-84).
            if (metricsRegistry().create_throttle_fail_open_total.fetch_add(1)
                == 0)
            {
                logStructured("WARN",
                              "create throttle fail-open: rate limiter "
                              "unavailable",
                              {{"component", "url_shortener"}});
            }
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
        args.add_string_map("metadata",
                            std::map<std::string, std::string>(
                                metadata->begin(), metadata->end()));
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

BeastResponse handleListLinks(const BeastRequest& req,
                              const ::ServerConfig& config,
                              const bool is_tls,
                              const RouteContext& context)
{
    // Gate (user decision 2026-11-16, threat model T-71): when the command API
    // is disabled (the secure default), answer exactly like today's
    // GET /api/v1/links so no caller reaches link.list and no enumeration
    // oracle exists. POST /api/v1/links shares this path, so today's router
    // returns 400 invalid_method "Only POST is supported"; reproduce it
    // byte-for-byte via the same makeApiErrorResponse the router's error
    // handler uses. The store is never read on this branch.
    if (!command_api_enabled(config)) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 400, "invalid_method",
            "Only POST is supported");
    }

    // Parse the query string (percent-decoded, repeated keys preserved). A
    // malformed query -> 400 invalid_request with a fixed, value-free message.
    std::vector<query_param> params;
    std::string parse_error;
    if (!parse_query_string(context.query_string, &params, &parse_error)) {
        return url_shortener::makeApiErrorResponse(
            req, config, is_tls, 400, "invalid_request",
            "malformed query string");
    }

    // Build the link.list args from the query (contract C1: the handler builds
    // args only; the filter semantics live in link.list / the lister). Each
    // name is validated against the args-object grammar before add_string (the
    // builder throws on a bad or duplicate name), and duplicates are a 400.
    // Integer args (offset, limit) that are plain base-10 integers are emitted
    // as JSON number tokens; everything else — including unknown names — is a
    // string, so the registry reports <arg>: wrong_type / <name>: unknown. The
    // endpoint is strict by design.
    commands::args_builder args;
    for (const query_param& param : params) {
        if (!is_valid_arg_name(param.name)) {
            return url_shortener::makeApiErrorResponse(
                req, config, is_tls, 400, "invalid_request",
                "invalid parameter name");
        }
        if (args.has(param.name)) {
            return url_shortener::makeApiErrorResponse(
                req, config, is_tls, 400, "invalid_request",
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

    // Execute as the gated caller (subtask 08): status is forwarded verbatim,
    // and the write/admin requirement for status=all|deleted is enforced inside
    // execute by the link.list permission hook, surfacing here as a 403
    // not_authorized envelope for a User caller (the bridge maps the codes).
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(config,
                                 "link.list",
                                 args.str(),
                                 requestIdFromRequest(req),
                                 command_api_caller(config, context.peer)),
        200);
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
    // Transport concern: shape-validate the JSON body and pick the exact
    // per-field 400 codes and their ORDER that the REST contract locks in
    // (contract C3), then run the mutation through the command registry as the
    // legacy Admin caller (contract C1, threat model T-60). Read-mutate-persist
    // and the business validation run once inside the link.update handler /
    // link command service UpdateLink - the single mutation path shared with
    // the CLI adapter; this handler holds no UpdateLinkCommand DTO nor error
    // switch.
    //
    // REST contract (C3): existence is checked BEFORE any body validation, so a
    // PATCH to an unknown slug returns 404 not_found even when the body is
    // malformed. The lookup runs link.get through the same bridge, keeping the
    // existence check in the adapter (a future move into the registry would
    // alter the REST ordering). The two registry calls (link.get, link.update)
    // are not atomic - exactly as the two legacy service calls were not.
    const auto slug = pathValue(context, "slug");

    // 1. Existence first: an unknown slug is 404 not_found whatever the body.
    {
        commands::args_builder existence_args;
        existence_args.add_string("slug", slug);
        const auto existing = execute_resource_command(
            config, "link.get", existence_args.str(), requestIdFromRequest(req));
        if (!existing.ok) {
            return resource_response(req, config, is_tls, existing);
        }
    }

    // The three-state locals below replace the old std::optional<std::optional>
    // DTO fields: absent / explicit null / value stay three distinct states that
    // the args object now carries (add_null vs add_string vs omitted).

    // 2. enabled: present but not a bool -> invalid_enabled.
    std::optional<bool> enabled;
    if (url_shortener::hasJsonField(req.body(), "enabled")) {
        enabled = url_shortener::extractJsonBoolField(req.body(), "enabled");
        if (!enabled.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_enabled",
                "enabled must be boolean");
        }
    }

    // 3. expires_at tri-state: token missing -> 400; explicit null -> clear; a
    // string parsing with parseRfc3339Zulu -> set; anything else -> 400.
    bool expires_present = false;
    bool expires_is_null = false;
    std::optional<std::string> expires;
    if (url_shortener::hasJsonField(req.body(), "expires_at")) {
        expires_present = true;
        const auto token =
            url_shortener::extractJsonValueToken(req.body(), "expires_at");
        if (!token.has_value()) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_expires_at",
                "expires_at must be RFC3339 UTC or null");
        }
        if (*token == "null") {
            expires_is_null = true;
        }
        else if (const auto parsed = url_shortener::extractJsonStringField(
                     req.body(), "expires_at");
                 parsed.has_value()
                 && url_shortener::parseRfc3339Zulu(*parsed).has_value())
        {
            expires = *parsed;
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

    // 4. tags: not a string array -> 400; fails validateTags -> 400; the
    // normalized (trimmed, de-duplicated) list is what is applied.
    std::optional<std::vector<std::string>> tags;
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
        auto normalized = *raw_tags;
        if (!validateTags(normalized)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_tags",
                "tags violate constraints");
        }
        tags = std::move(normalized);
    }

    // 5. metadata: not a flat string object, or fails validateMetadata -> 400.
    std::optional<std::unordered_map<std::string, std::string>> metadata;
    if (url_shortener::hasJsonField(req.body(), "metadata")) {
        auto parsed = url_shortener::extractJsonFlatObjectStringField(
            req.body(), "metadata");
        if (!parsed.has_value() || !validateMetadata(*parsed)) {
            return url_shortener::makeApiErrorResponse(
                req,
                config,
                is_tls,
                400,
                "invalid_metadata",
                "metadata must be flat object with string values");
        }
        metadata = std::move(parsed);
    }

    // 6. campaign tri-state: explicit null -> clear; not an object -> 400; fails
    // validateCampaign -> 400. The object forwarded to the registry is rebuilt
    // from only the six known members, so unknown inner keys are ignored (parity)
    // and never reach the registry's object validator.
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

    // --- Build the link.update args from the validated locals (known keys
    // only, so unknown top-level keys are ignored). slug is the addressing arg;
    // target_url/url never appears here, so the target stays immutable through
    // PATCH. An empty or non-JSON body leaves only slug, which the service
    // treats as a no-op update that still refreshes updated_at and returns 200.
    commands::args_builder args;
    args.add_string("slug", slug);
    if (enabled.has_value()) {
        args.add_bool("enabled", *enabled);
    }
    if (expires_present) {
        if (expires_is_null) {
            args.add_null("expires_at");
        }
        else {
            args.add_string("expires_at", *expires);
        }
    }
    if (tags.has_value()) {
        args.add_string_list("tags", *tags);
    }
    if (metadata.has_value()) {
        args.add_string_map("metadata",
                            std::map<std::string, std::string>(
                                metadata->begin(), metadata->end()));
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
            config, "link.update", args.str(), requestIdFromRequest(req)),
        200);
}

BeastResponse handleDeleteLink(const BeastRequest& req,
                               const ::ServerConfig& config,
                               const bool is_tls,
                               const RouteContext& context)
{
    // Transport concern: address the link by slug and run the soft-delete
    // through the command bridge as the legacy Admin caller (contract C1,
    // threat model T-60). The read-mutate-persist logic and the not_found path
    // live in the link.delete handler / link command service DeleteLink, the
    // single mutation path shared with the CLI adapter; this handler holds no
    // DeleteLinkCommand DTO nor error-code switch. No request body is read;
    // statuses, bodies and error codes are produced by the bridge (contract
    // C3). The redirect fast path is unaffected (contract C10): this is the
    // management plane.
    commands::args_builder args;
    args.add_string("slug", pathValue(context, "slug"));
    return resource_response(
        req,
        config,
        is_tls,
        execute_resource_command(
            config, "link.delete", args.str(), requestIdFromRequest(req)),
        200);
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
