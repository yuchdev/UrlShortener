#pragma once

#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <boost/beast/http.hpp>
#include <url_shortener/core/config.hpp>
#include <url_shortener/core/types.hpp>

namespace url_shortener
{

// ---------------------------------------------------------------------------
// Time utilities
// ---------------------------------------------------------------------------

/**
 * @brief Portable cross-platform wrapper around `timegm` (POSIX) /
 * `_mkgmtime` (Windows).
 *
 * Converts a broken-down UTC time to a `time_t` without adjusting
 * for the local timezone.
 * @param tm Broken-down UTC time; modified by the underlying call on
 * some platforms.
 * @return Corresponding UTC `time_t`.
 */
std::time_t timegm_utc(std::tm* tm);

/**
 * @brief Portable cross-platform wrapper around `gmtime_r` (POSIX) /
 * `gmtime_s` (Windows).
 *
 * Fills `tm` with the UTC broken-down representation of `*t` without
 * adjusting for the local timezone.
 * @param t UTC `time_t` to convert.
 * @param tm Output buffer for the broken-down time.
 */
void gmtime_utc(const std::time_t* t, std::tm* tm);

/**
 * @brief Returns the current wall-clock time as an ISO 8601 / RFC3339
 * UTC string (e.g. `"2025-01-15T12:34:56Z"`).
 * @return Current timestamp formatted as `"%Y-%m-%dT%H:%M:%SZ"`.
 */
std::string currentTimestamp();

/**
 * @brief Formats a `system_clock::time_point` as an ISO 8601 / RFC3339
 * UTC string (e.g. `"2025-01-15T12:34:56Z"`).
 * @param time_point The time point to format.
 * @return The timestamp formatted as `"%Y-%m-%dT%H:%M:%SZ"`.
 */
std::string formatTimestamp(
    const std::chrono::system_clock::time_point time_point);

/**
 * @brief Parses a strict RFC3339 Zulu timestamp string.
 *
 * Accepts only the `"%Y-%m-%dT%H:%M:%SZ"` format; sub-second
 * precision and non-Zulu offsets are not supported.
 * @param value String to parse.
 * @return The parsed time point; `std::nullopt` if `value` does not
 * match the expected format.
 */
std::optional<std::chrono::system_clock::time_point> parseRfc3339Zulu(
    const std::string& value);

/**
 * @brief Checks whether a nullable expiry timestamp is in the past.
 *
 * Returns `false` if `expires_at` is absent or cannot be parsed by
 * `parseRfc3339Zulu`, making the check fail-open (treats
 * unparseable timestamps as not expired).
 * @param expires_at Optional RFC3339 Zulu expiry timestamp string.
 * @return `true` if `expires_at` is present, parseable, and its value
 * is <= `std::chrono::system_clock::now()`; `false` otherwise.
 */
bool isExpired(const std::optional<std::string>& expires_at);

// ---------------------------------------------------------------------------
// String manipulation
// ---------------------------------------------------------------------------

/**
 * @brief Strips leading and trailing ASCII whitespace (space, tab,
 * CR, LF) from `value`.
 * @param value Input string to trim.
 * @return Trimmed copy; empty string if `value` is all whitespace.
 */
std::string trim(const std::string& value);

/**
 * @brief Returns `value` truncated to at most `max_len` bytes.
 *
 * If `value.size() <= max_len` the original string is returned
 * unchanged. No UTF-8 boundary alignment is performed; the truncation
 * may split a multi-byte character.
 * @param value String to clamp.
 * @param max_len Maximum byte length of the result.
 * @return A string of at most `max_len` bytes.
 */
std::string clampString(const std::string& value, size_t max_len);

/**
 * @brief Encodes raw bytes as a lowercase hexadecimal string.
 * @param data Pointer to the byte array to encode.
 * @param len Number of bytes to encode.
 * @return Lowercase hex string of length `2 * len`.
 */
std::string hexEncode(const unsigned char* data, size_t len);

// ---------------------------------------------------------------------------
// ID generation
// ---------------------------------------------------------------------------

/**
 * @brief Generates a random alphanumeric slug of the requested length.
 *
 * Uses a thread-local `std::mt19937` seeded from `std::random_device`
 * on first use. Characters are drawn from
 * `[0-9A-Za-z]` (62 symbols). The generated slug is NOT
 * cryptographically secure; do not use it for secrets.
 * @param length Number of characters to generate; must be > 0.
 * @return Random alphanumeric string of exactly `length` characters.
 */
std::string generateSlug(const uint32_t length);

/**
 * @brief Generates a random 20-character opaque identifier.
 *
 * Delegates to `generateSlug(20)`. The result is NOT a UUID and NOT
 * cryptographically secure; it is used as a stable link identifier
 * in the metadata store.
 * @return 20-character random alphanumeric identifier.
 */
std::string generateId();

// ---------------------------------------------------------------------------
// Validation and normalization
// ---------------------------------------------------------------------------

/**
 * @brief Returns `true` if `url` starts with `"http://"` or
 * `"https://"` (prefix-only check; does not validate the full URL).
 * @param url String to check.
 * @return `true` for HTTP/HTTPS URLs; `false` otherwise.
 */
bool isValidHttpUrl(const std::string& url);

/**
 * @brief Returns `true` if `slug` matches the allowed slug format.
 *
 * Accepted pattern: `^[A-Za-z0-9][A-Za-z0-9_-]{2,63}$` — first
 * character alphanumeric, total length 3–64 characters, only
 * alphanumeric, underscore, and hyphen characters.
 * @param slug Slug string to validate.
 * @return `true` if `slug` matches the regex; `false` otherwise.
 */
bool isValidSlug(const std::string& slug);

/**
 * @brief Returns `true` if `slug` conflicts with a system-reserved
 * path segment.
 *
 * Reserved set (case-insensitive): `"api"`, `"health"`, `"metrics"`,
 * `"admin"`, `"login"`, `"logout"`, `"r"`, `"preview"`, `"stats"`.
 * @param slug Slug string to check; matched case-insensitively.
 * @return `true` if `slug` is reserved; `false` otherwise.
 */
bool isReservedSlug(const std::string& slug);

/**
 * @brief Splits a URL into its scheme and the authority+path remainder.
 *
 * Uses the regex `^([A-Za-z][A-Za-z0-9+.-]*):\/\/([^\/\?#]+)(.*)$`.
 * @param input_url URL to split.
 * @return A pair of `{scheme, authority_path_rest}` on success;
 * `std::nullopt` if `input_url` does not match the pattern.
 */
std::optional<std::pair<std::string, std::string>> splitUrl(
    const std::string& input_url);

/**
 * @brief Returns `true` if `host` resolves to a private, loopback,
 * link-local, or RFC 1918/4193/4291 address.
 *
 * Covers: `localhost`, `::1`, `0.0.0.0`, `[::]`, `.internal` and
 * `.local` domain suffixes, `127.x`, `10.x`, `192.168.x`,
 * `169.254.x`, `172.16–31.x`, and IPv6 ULA (`fc`/`fd`) and
 * link-local (`fe80:`) prefixes. Bracketed IPv6 addresses are
 * unbracketed before matching.
 * @param host Hostname or IP address to check (without port).
 * @return `true` if `host` is a private/loopback address.
 */
bool isPrivateHost(const std::string& host);

/**
 * @brief Normalizes and validates `input_url` as a redirect target.
 *
 * Steps: trim whitespace → parse scheme+authority via `splitUrl` →
 * require `http` or `https` scheme → reject userinfo (`@`) →
 * lowercase the hostname → strip default ports (80 for http, 443
 * for https) → reject private hosts unless
 * `config.shortener_allow_private_targets` is set.
 * @param input_url Raw target URL supplied by the caller.
 * @param config Server configuration; `shortener_allow_private_targets`
 * controls whether private-host targets are permitted.
 * @return Normalized absolute URL on success; `std::nullopt` if the
 * URL is empty, not an HTTP/HTTPS URL, contains userinfo, or its
 * host is private and private targets are disabled.
 */
std::optional<std::string> normalizeTargetUrl(const std::string& input_url,
                                              const ServerConfig& config);

/**
 * @brief Validates and normalizes `raw_domain` as a base domain for
 * short-URL construction.
 *
 * Requirements: non-empty, absolute HTTP/HTTPS URL, no userinfo,
 * no path (trailing slashes are stripped), no query or fragment.
 * Returns the canonicalized `scheme://authority` string.
 * @param raw_domain Raw base-domain string from config or CLI.
 * @return Normalized `"scheme://host[:port]"` string.
 * @throws std::runtime_error if `raw_domain` is empty, missing a
 * scheme, has a non-HTTP/HTTPS scheme, contains userinfo, or
 * contains a non-trailing path, query, or fragment component.
 */
std::string normalizeAndValidateBaseDomain(const std::string& raw_domain);

// ---------------------------------------------------------------------------
// Cryptography helpers
// ---------------------------------------------------------------------------

/**
 * @brief Computes HMAC-SHA256(`key=salt`, `data=raw_client`) and
 * returns the result as a lowercase hex string.
 *
 * Used by the analytics pipeline to hash raw client identifiers
 * before storage, so that raw IPs/identifiers are never persisted.
 * The result is 64 hex characters (256-bit digest).
 * @param raw_client Raw client identifier to hash (e.g. IP address).
 * @param salt Secret HMAC key (see
 * `ServerConfig::analytics_client_hash_salt`); treat as secret and
 * do not log.
 * @return 64-character lowercase hex-encoded HMAC-SHA256 digest.
 */
std::string hashClientIdentifier(const std::string& raw_client,
                                 const std::string& salt);

// ---------------------------------------------------------------------------
// HTTP helpers
// ---------------------------------------------------------------------------

/**
 * @brief Extracts a request header value and clamps it to `max_len`
 * bytes.
 * @param req The HTTP request to extract the header from.
 * @param field The header field to extract.
 * @param max_len Maximum byte length; the value is passed through
 * `clampString` if it exceeds this limit.
 * @return The (possibly clamped) header value; `std::nullopt` if the
 * header is not present in the request.
 */
std::optional<std::string> headerString(
    const boost::beast::http::request<boost::beast::http::string_body>& req,
    const boost::beast::http::field field,
    const size_t max_len);

// ---------------------------------------------------------------------------
// JSON helpers
// (manual string-scanning; see CLAUDE.md "JSON parsing" - no JSON library)
// ---------------------------------------------------------------------------

/**
 * @brief Escapes a string so the result is always a valid JSON string body
 * (no surrounding quotes), regardless of what bytes @p input contains.
 *
 * Shared by every JSON serializer in the codebase (REST bodies, command
 * results, `db.*` rows), so it treats @p input as fully untrusted and
 * guarantees well-formed, control-character-free output:
 * - `\`, `"`, `\n`, `\r`, and `\t` become their short JSON backslash
 *   sequences (unchanged from the historical behavior).
 * - every other ASCII control byte (`< 0x20`) and `0x7F` (DEL) become a
 *   `\u00XX` escape, so no raw control byte ever reaches the output.
 * - the Unicode line/paragraph separators U+2028 and U+2029 become
 *   ` `/` ` (valid JSON but invalid in JavaScript string
 *   literals, so escaping them keeps embedded output safe).
 * - any byte sequence that is not valid UTF-8 (a lone continuation byte, a
 *   truncated multi-byte tail, an overlong encoding, a surrogate code
 *   point, or a code point above U+10FFFF) is replaced with the Unicode
 *   replacement character U+FFFD (`EF BF BD`); one replacement is emitted
 *   per offending byte and decoding resynchronizes at the next byte.
 *
 * Valid UTF-8 text that contains none of the escaped code points is copied
 * through byte-for-byte, so already-valid inputs serialize identically to
 * the historical implementation.
 *
 * @param input Raw, untrusted string to escape.
 * @return Escaped string without surrounding quotes; always valid as the
 * contents of a JSON string literal.
 */
std::string jsonEscape(const std::string& input);

/**
 * @brief Wraps `value` in double quotes after JSON-escaping it.
 *
 * Equivalent to `'"' + jsonEscape(value) + '"'`.
 * @param value String to quote.
 * @return JSON string literal including surrounding double quotes.
 */
std::string jsonString(const std::string& value);

/**
 * @brief Extracts the string value of a JSON field by naive string
 * scanning.
 *
 * Locates the first occurrence of `"<field>"` in `body`, then scans
 * forward past `:` for a quoted string value. Handles `\"` escape
 * sequences but not other JSON escape forms (e.g. `\uXXXX`).
 * @param body Raw JSON request body.
 * @param field Field name to look up (without quotes).
 * @return The unescaped field value; `std::nullopt` if the field is
 * absent, its value is not a string, or the string is unterminated.
 */
std::optional<std::string> extractJsonStringField(const std::string& body,
                                                  const std::string& field);

/**
 * @brief Extracts the boolean value of a JSON field by naive string
 * scanning.
 *
 * Locates `"<field>"` in `body`, scans past `:`, and matches `true`
 * or `false` literally.
 * @param body Raw JSON request body.
 * @param field Field name to look up (without quotes).
 * @return `true` or `false` on match; `std::nullopt` if the field is
 * absent or its value is neither `true` nor `false`.
 */
std::optional<bool> extractJsonBoolField(const std::string& body,
                                         const std::string& field);

/**
 * @brief Checks whether `"<field>"` appears anywhere in `body`.
 *
 * Does not validate that the key is followed by a colon; a false
 * positive is possible if the field name appears inside a string
 * value.
 * @param body Raw JSON request body.
 * @param field Field name to search for (without quotes).
 * @return `true` if `"<field>"` is found; `false` otherwise.
 */
bool hasJsonField(const std::string& body, const std::string& field);

/**
 * @brief Extracts the raw JSON value token for `field`.
 *
 * Returns `"null"` for a JSON `null`; the unescaped string content
 * for a string value; or the full raw token for array (`[...]`) and
 * object (`{...}`) values (balanced bracket scanning). Does not
 * handle bare number or boolean tokens.
 * @param body Raw JSON request body.
 * @param field Field name to look up (without quotes).
 * @return The raw JSON value token; `"null"` for JSON null;
 * `std::nullopt` if the field is absent, the string is unterminated,
 * or bracket balancing fails.
 */
std::optional<std::string> extractJsonValueToken(const std::string& body,
                                                 const std::string& field);

/**
 * @brief Extracts a JSON array of strings for `field`.
 *
 * Uses `extractJsonValueToken` to get the raw array token, then
 * parses it element by element. Returns an empty vector for a JSON
 * `null` value. Values with escaped characters other than `\"` and
 * `\\` are passed through without further unescape processing.
 * @param body Raw JSON request body.
 * @param field Field name to look up (without quotes).
 * @return A vector of unescaped strings; an empty vector for a JSON
 * `null`; `std::nullopt` if the field is absent, the value is not an
 * array of strings, or parsing fails.
 */
std::optional<std::vector<std::string>> extractJsonStringArrayField(
    const std::string& body, const std::string& field);

/**
 * @brief Extracts a flat JSON object whose keys and values are both
 * strings.
 *
 * Uses regex matching on the raw object token; values containing
 * escaped quotes (`\"`) in their content may not be extracted
 * correctly. Returns an empty map for a JSON `null` value.
 * @param body Raw JSON request body.
 * @param field Field name to look up (without quotes).
 * @return A `string → string` map of the object's key/value pairs;
 * an empty map for a JSON `null`; `std::nullopt` if the field is
 * absent, the token is not an object, or any value is not a plain
 * string.
 */
std::optional<std::unordered_map<std::string, std::string>>
extractJsonFlatObjectStringField(const std::string& body,
                                 const std::string& field);

// ---------------------------------------------------------------------------
// Enum conversions
// ---------------------------------------------------------------------------

/**
 * @brief Parses a redirect type from its string representation.
 * @param value `"temporary"` or `"permanent"` (exact match, case-
 * sensitive).
 * @return The corresponding `RedirectType`; `std::nullopt` if `value`
 * is neither `"temporary"` nor `"permanent"`.
 */
std::optional<RedirectType> parseRedirectType(const std::string& value);

/**
 * @brief Converts a `RedirectType` to its lowercase string name.
 * @param value Enum value to convert.
 * @return `"temporary"` or `"permanent"`.
 */
std::string redirectTypeToString(const RedirectType value);

/**
 * @brief Converts a `LinkStatus` to its lowercase string name.
 * @param status Enum value to convert.
 * @return `"active"`, `"disabled"`, `"expired"`, or `"deleted"`;
 * returns `"active"` for unrecognised values.
 */
std::string linkStatusToString(const LinkStatus status);

// ---------------------------------------------------------------------------
// Link utilities
// ---------------------------------------------------------------------------

/**
 * @brief Derives the computed lifecycle status of a link record.
 *
 * Priority order: `deleted_at` set → `deleted`; `!enabled` →
 * `disabled`; `isExpired(expires_at)` → `expired`; otherwise
 * `active`.
 * @param link Link record to evaluate.
 * @return The highest-priority applicable `LinkStatus`.
 */
LinkStatus resolveLinkStatus(const Link& link);

/**
 * @brief Returns the Boost.Beast HTTP status code for the redirect
 * response appropriate to `link.redirect_type`.
 * @param link Link whose `redirect_type` determines the status.
 * @return `moved_permanently` (301) for `RedirectType::permanent`;
 * `found` (302) for `RedirectType::temporary`.
 */
boost::beast::http::status redirectStatusFor(const Link& link);

// ---------------------------------------------------------------------------
// Validation and guard hooks
// ---------------------------------------------------------------------------

/**
 * @brief Validates and normalizes a tag list in place.
 *
 * Rules enforced: at most 20 tags; each tag trimmed and must be
 * non-empty after trimming; each tag at most 32 characters; each
 * tag may only contain alphanumeric characters, `':'`, `'-'`, and
 * `'_'`. Duplicate tags (after trimming) are silently removed; the
 * order of surviving tags is preserved.
 * @param tags Tag list to validate; modified in place: trimmed,
 * deduplicated, and replaced with the normalized subset on success.
 * @return `true` if all tags pass validation after normalization;
 * `false` if the list exceeds 20 entries, any tag is empty after
 * trimming, any tag exceeds 32 characters, or any tag contains a
 * disallowed character.
 */
bool validateTags(std::vector<std::string>& tags);

/**
 * @brief Validates a flat string-to-string metadata map.
 *
 * Rules: at most 50 key/value pairs; each key non-empty and at most
 * 64 characters; each value at most 512 characters.
 * @param metadata Map to validate; not modified.
 * @return `true` if all constraints are satisfied; `false` if the
 * map has more than 50 entries, any key is empty or longer than 64
 * characters, or any value is longer than 512 characters.
 */
bool validateMetadata(
    const std::unordered_map<std::string, std::string>& metadata);

/**
 * @brief Validates UTM-style campaign attribution fields.
 *
 * Each present field must be at most 128 characters. An absent
 * `campaign` (`std::nullopt`) is always valid.
 * @param campaign Optional campaign to validate.
 * @return `true` if `campaign` is absent or all present fields are
 * <= 128 characters; `false` if any present field exceeds the limit.
 */
bool validateCampaign(const std::optional<Link::Campaign>& campaign);

/**
 * @brief Password-guard hook for redirect requests.
 *
 * @note This is a stub implementation that always returns `true`
 * (no password protection). It is a placeholder for future
 * per-link password enforcement.
 * @param link The link being accessed.
 * @param req The inbound HTTP request.
 * @return Always `true` in the current implementation.
 */
bool passwordGuardCheck(
    const Link& link,
    const boost::beast::http::request<boost::beast::http::string_body>& req);

/**
 * @brief Rate-limit guard hook for redirect requests.
 *
 * @note This is a stub implementation that always returns `true`
 * (no rate limiting applied). It is a placeholder for future
 * per-link rate limiting via `IRateLimiter`.
 * @param link The link being accessed.
 * @param req The inbound HTTP request.
 * @return Always `true` in the current implementation.
 */
bool rateLimitGuardAllow(
    const Link& link,
    const boost::beast::http::request<boost::beast::http::string_body>& req);

}  // namespace url_shortener
