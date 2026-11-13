#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include <url_shortener/commands/error_codes.hpp>

namespace url_shortener::commands
{

/**
 * @brief The two mode flags carried by the request envelope's `options` object.
 *
 * Both default to `false` (the catalog fixes exactly these two fields; any
 * other `options` key makes the envelope `invalid_envelope`). The flags are
 * only parsed here; whether a given command actually honors them is decided
 * later by the registry (`unsupported` when it does not).
 */
struct CommandOptions
{
    ///< `true` when the caller requested a validate-and-authorize-only run with
    ///< no mutation; mirrors `options.dry_run`. Honored only where the catalog
    ///< says so, `unsupported` elsewhere.
    bool dry_run = false;

    ///< `true` when the caller asked to run an async-capable command as a job;
    ///< mirrors `options.async`. Honored only where the catalog says so.
    bool async = false;
};

/**
 * @brief One parsed, envelope-validated command request (wire shape v1).
 *
 * Produced by @ref parse_request from a single request-envelope JSON text. The
 * envelope rules (version, id, command-name shape, `args` is an object, closed
 * `options`) have all passed by the time this struct exists; the *contents* of
 * `args` are still unvalidated (that is `ArgSpec`, a later subtask). Every
 * string field is safe to log and to echo: `id` is a validated client id or a
 * server-generated fallback, `command` matched @ref is_valid_command_name.
 */
struct CommandRequest
{
    ///< Envelope version; always `1` once parsed (the only version defined).
    int v = 1;

    ///< Correlation id echoed back in the response: either the client's `id`
    ///< after it passed the id rules, or a server-generated `"srv-<n>"` when
    ///< the client sent none. Never raw, unvalidated client input.
    std::string id;

    ///< Dotted command name, validated for shape only (@ref
    ///< is_valid_command_name); not yet resolved against the registry.
    std::string command;

    ///< Exact JSON source text of the `args` object, stored verbatim (never
    ///< re-serialized); `"{}"` when the envelope omitted `args`. Its contents
    ///< are not validated here.
    std::string args_json = "{}";

    ///< The parsed `options` flags; both `false` when `options` was omitted.
    CommandOptions options;
};

/**
 * @brief Hard bounds @ref parse_request applies to one request envelope.
 *
 * These are passed in (rather than read from `ServerConfig`) so the commands
 * layer stays free of server-config coupling; the adapter copies the matching
 * `ServerConfig` fields into this struct. The byte cap is defense in depth: the
 * REST adapter already enforces `max_request_body_bytes` before calling.
 */
struct envelope_limits
{
    ///< Maximum accepted envelope size in bytes, checked by the scanner before
    ///< parsing; a larger input is rejected as `too_large`. Mirrors
    ///< `ServerConfig::max_request_body_bytes`.
    std::size_t max_bytes = 65536;

    ///< Maximum accepted length of the client `id` in bytes; a longer `id` is
    ///< treated as absent for the echoed id and rejected as `invalid_envelope`.
    ///< Mirrors `ServerConfig::request_id_max_length`.
    std::size_t max_id_len = 64;

    ///< Maximum accepted length of the decoded `command` name in bytes; a
    ///< longer name is rejected as `invalid_envelope`.
    std::size_t max_command_len = 64;

    ///< Maximum JSON nesting depth passed through to the scanner; exceeding it
    ///< is rejected as `too_deep`. Mirrors the scanner's default.
    int max_depth = 8;
};

/**
 * @brief Outcome of @ref parse_request: either a validated request or a precise
 * `invalid_envelope` error, plus the id to echo in the response.
 *
 * Exactly one of two states holds: on success `request` has a value and `error`
 * is `none`; on failure `request` is empty and `error` carries
 * `invalid_envelope` with a fixed, value-free message. `id` is always populated
 * with the id the response must echo (see @ref id), so a caller can build an
 * error envelope without re-deriving the id.
 */
struct parse_request_result
{
    ///< The parsed request on success; empty on failure.
    std::optional<CommandRequest> request;

    ///< The failure detail when `request` is empty; `code == none` on success.
    ///< Always `invalid_envelope` on failure, with a fixed message naming only
    ///< the offending field, never echoing any input value.
    CommandError error;

    ///< The id to echo in the response in either outcome: the client's `id`
    ///< when it passed the id rules (1..`max_id_len` bytes, no control chars),
    ///< otherwise a server-generated `"srv-<n>"`. Never raw client input, so it
    ///< is safe to place in a response and an audit line even on failure.
    std::string id;

    /**
     * @brief Indicates whether parsing produced a request.
     * @return `true` when `request` has a value (success); `false` otherwise.
     */
    bool ok() const { return request.has_value(); }
};

/**
 * @brief Parses one request-envelope JSON text into a @ref CommandRequest.
 *
 * Enforces the envelope rules of docs/commands/README.md section 1 using the
 * one sanctioned scanner (`json_scan.hpp`): the text is a single JSON object;
 * `v` is the integer `1`; `command` is a string matching @ref
 * is_valid_command_name within `max_command_len`; an optional `id` is a string
 * of 1..`max_id_len` bytes with no control characters (absent -> @ref
 * next_server_id); an optional `args` is an object stored verbatim (absent ->
 * `"{}"`); an optional `options` object carries only boolean `dry_run`/`async`.
 * The envelope is closed: any unknown top-level or `options` key, and any
 * duplicate key, is rejected. No failure message echoes any substring of the
 * input.
 *
 * The id is resolved first, so a response to a *failed* parse still echoes the
 * client id when that id was itself valid; an absent, overlong or control-char
 * id is replaced by a `"srv-<n>"` fallback (never the hostile value).
 *
 * @param text The candidate envelope JSON. Treated as untrusted; never logged.
 * @param limits Byte/id-length/command-length/depth caps; defaults mirror the
 * server's request-hardening limits.
 * @return A @ref parse_request_result. On success `ok()` is `true`, `request`
 * holds the parsed command, and `error.code == none`. On failure `ok()` is
 * `false`, `request` is empty, and `error.code == invalid_envelope` with a
 * fixed message for the first rule violated (`malformed`, `too_large`,
 * `too_deep`, `duplicate_key`, or a field-naming message). In both outcomes
 * `id` holds the id to echo.
 */
parse_request_result parse_request(std::string_view text,
                                   const envelope_limits& limits = {});

/**
 * @brief Returns the next process-wide server-generated correlation id.
 *
 * Backed by a function-local `std::atomic<std::uint64_t>`, so the returned ids
 * are unique and strictly increasing across threads for the life of the
 * process. The id carries no meaning for the server and is not unguessable; it
 * exists only to correlate a response with a request that supplied no `id`.
 *
 * @return A fresh id of the form `"srv-<n>"` with a monotonically increasing
 * `<n>` starting at `1`.
 */
std::string next_server_id();

/**
 * @brief Tests whether @p name is a well-formed dotted command name.
 *
 * Accepts exactly the grammar `^[a-z][a-z0-9_]*(\.[a-z][a-z0-9_]*)+$`
 * (lower-case letters, digits and underscores in dot-separated segments, each
 * segment beginning with a letter, at least two segments). Implemented by hand;
 * no `<regex>`. This checks name *shape* only; it does not consult the registry
 * and so says nothing about whether such a command exists.
 *
 * @param name The candidate command name (already decoded from JSON).
 * @return `true` when @p name matches the grammar; `false` otherwise, including
 * for the empty string, an upper-case letter, a leading/trailing/doubled dot,
 * a single segment with no dot, or any other character.
 */
bool is_valid_command_name(std::string_view name);

} // namespace url_shortener::commands
