#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace url_shortener::commands
{

/**
 * @brief Classification of a JSON value by its first significant character.
 *
 * The scanner records the kind of every top-level member/element so callers
 * can decide how to interpret `raw` without re-inspecting it. Container kinds
 * (`object`, `array`) are returned as raw text only; a caller re-scans a
 * nested container by passing its `raw` back to `scan_object`/`scan_array`.
 */
enum class json_kind
{
    ///< A JSON string value; the associated `raw` includes the surrounding
    ///< double quotes and any escape sequences verbatim.
    string,

    ///< A JSON number value validated against the JSON number grammar (no
    ///< leading `+`, no leading zeros, optional fraction/exponent).
    number,

    ///< A JSON boolean literal spelled exactly `true` or `false`.
    boolean,

    ///< The JSON `null` literal.
    null,

    ///< A nested JSON object; `raw` is its exact source text, brace to brace.
    object,

    ///< A nested JSON array; `raw` is its exact source text, bracket to
    ///< bracket.
    array
};

/**
 * @brief One top-level member of a scanned JSON object.
 *
 * Produced by `scan_object`. The value is kept as raw source text (not
 * decoded) so the scanner stays allocation-light and transport-agnostic;
 * callers decode leaf values themselves (string values via
 * `decode_json_string`, numbers via their own conversion).
 */
struct json_member
{
    ///< The member key, already decoded from its JSON string token to UTF-8.
    ///< Guaranteed free of embedded NUL (such a key is rejected as
    ///< `malformed`) and no longer than `scan_limits::max_key_len` bytes.
    std::string key;

    ///< Kind of the member's value, classified from its first character.
    json_kind kind;

    ///< Exact source text of the value. For `string` values this includes
    ///< the surrounding quotes and raw escapes; for `object`/`array` values
    ///< it is the full nested text to be re-scanned on demand.
    std::string raw;
};

/**
 * @brief Hard bounds enforced by the scanner on a single input document.
 *
 * These are the whole input-hardening perimeter for the commands layer: the
 * scanner is the only JSON tokenizer in product code, so every caller relies
 * on these caps to bound memory and work on hostile input. Defaults mirror the
 * server's request-hardening limits.
 */
struct scan_limits
{
    ///< Maximum accepted input size in bytes, checked before any parsing;
    ///< a larger input is rejected as `too_large`. Mirrors
    ///< `ServerConfig::max_request_body_bytes`.
    std::size_t max_bytes = 65536;

    ///< Maximum nesting depth of objects/arrays *inside* a value; the
    ///< scanned top-level object/array itself does not count. Exceeding it
    ///< is rejected as `too_deep`.
    int max_depth = 8;

    ///< Maximum number of top-level members (object) or elements (array);
    ///< exceeding it is rejected as `too_many_members`.
    std::size_t max_members = 256;

    ///< Maximum decoded key length in bytes; a longer key is rejected as
    ///< `malformed`.
    std::size_t max_key_len = 128;
};

/**
 * @brief Outcome of scanning a JSON object into its top-level members.
 *
 * On failure `error` carries one of a small fixed vocabulary of codes; it
 * never echoes any portion of the scanned input, so it is safe to log or
 * return to an untrusted caller.
 */
struct scan_result
{
    ///< `true` only when the input was exactly one well-formed JSON object
    ///< within all limits; `false` otherwise, with `error` set.
    bool ok = false;

    ///< The top-level members in source order; empty for `{}`. Meaningful
    ///< only when `ok` is `true`.
    std::vector<json_member> members;

    ///< Failure reason when `ok` is `false`, from the fixed vocabulary:
    ///< `"not_an_object"`, `"too_large"`, `"too_deep"`,
    ///< `"too_many_members"`, `"duplicate_key"`, `"malformed"`,
    ///< `"trailing_data"`. Empty when `ok` is `true`.
    std::string error;
};

/**
 * @brief Scans @p text, which must be exactly one JSON object.
 *
 * Leading and trailing JSON whitespace is allowed. The scan is iterative (no
 * recursion), runs in O(n) over the input, and never throws for malformed or
 * hostile input. It enumerates only true top-level members: keys that appear
 * inside nested objects or inside string values are never mistaken for
 * top-level keys (the regression the `core/utils.hpp` first-occurrence search
 * suffers). Duplicate top-level keys are rejected.
 *
 * @param text The candidate JSON object text. Treated as untrusted; never
 * logged by the scanner.
 * @param limits Byte/depth/member/key-length caps enforced before and during
 * the scan; defaults mirror the server's request-hardening limits.
 * @return A `scan_result` with `ok == true` and the members on success. On
 * failure `ok == false` and `error` is one of: `"too_large"` (input exceeds
 * `max_bytes`), `"not_an_object"` (input is empty or its first non-whitespace
 * token is not `{`), `"too_deep"` (a value nests deeper than `max_depth`),
 * `"too_many_members"` (more than `max_members` members), `"duplicate_key"`
 * (a top-level key repeats), `"trailing_data"` (non-whitespace after the
 * closing `}`), or `"malformed"` (any other syntax error, an over-length key,
 * or a key that decodes to text containing an embedded NUL).
 */
scan_result scan_object(std::string_view text, const scan_limits& limits = {});

/**
 * @brief Decodes a JSON string token (including its surrounding quotes) to
 * UTF-8.
 *
 * Handles the JSON escapes `\"`, `\\`, `\/`, `\b`, `\f`, `\n`, `\r`, `\t` and
 * `\uXXXX`, including a high/low surrogate pair that combines into one
 * code point. The decoded result may contain an embedded NUL if the token
 * held `\u0000`; callers that forbid NUL (keys, string arguments) must check
 * for it themselves.
 *
 * @param raw_token The exact source text of a JSON string, including the
 * opening and closing double quotes.
 * @return The decoded UTF-8 string, or `std::nullopt` if @p raw_token is not a
 * quote-delimited string, contains an invalid escape (e.g. `\x`), an
 * incomplete `\uXXXX`, a lone/unpaired surrogate, or an unterminated string.
 */
std::optional<std::string> decode_json_string(std::string_view raw_token);

/**
 * @brief Finds the first member with the given key.
 *
 * @param members The members returned by `scan_object` (which already rejects
 * duplicate keys, so "first" is also "only").
 * @param key The key to look up; compared by value against each decoded key.
 * @return A pointer to the matching member, or `nullptr` if none matches. The
 * pointer is valid only while @p members is unmodified and alive.
 */
const json_member* find_member(const std::vector<json_member>& members,
                               std::string_view key);

/**
 * @brief One top-level element of a scanned JSON array.
 *
 * Produced by `scan_array`. Like `json_member`, the value is raw source text;
 * nested containers are re-scanned on demand.
 */
struct json_element
{
    ///< Kind of the element's value, classified from its first character.
    json_kind kind;

    ///< Exact source text of the element value, following the same rules as
    ///< `json_member::raw`.
    std::string raw;
};

/**
 * @brief Outcome of scanning a JSON array into its top-level elements.
 *
 * Mirrors `scan_result`; `error` uses the same fixed vocabulary except that a
 * non-array input yields `"not_an_array"` instead of `"not_an_object"`.
 */
struct array_scan_result
{
    ///< `true` only when the input was exactly one well-formed JSON array
    ///< within all limits; `false` otherwise, with `error` set.
    bool ok = false;

    ///< The top-level elements in source order; empty for `[]`. Meaningful
    ///< only when `ok` is `true`.
    std::vector<json_element> elements;

    ///< Failure reason when `ok` is `false`, from the fixed vocabulary:
    ///< `"not_an_array"`, `"too_large"`, `"too_deep"`,
    ///< `"too_many_members"`, `"malformed"`, `"trailing_data"`. Empty when
    ///< `ok` is `true`.
    std::string error;
};

/**
 * @brief Scans @p text, which must be exactly one JSON array, into its
 * top-level elements.
 *
 * Shares the state machine, limits and guarantees of `scan_object`:
 * iterative, O(n), never throws. `scan_limits::max_members` caps the element
 * count. Used for `string_list` arguments and by the TUI to split
 * `result.items` / `rows`.
 *
 * @param text The candidate JSON array text. Treated as untrusted; never
 * logged by the scanner.
 * @param limits Byte/depth/member caps enforced before and during the scan.
 * @return An `array_scan_result` with `ok == true` and the elements on
 * success. On failure `ok == false` and `error` is one of: `"too_large"`,
 * `"not_an_array"` (input is empty or its first non-whitespace token is not
 * `[`), `"too_deep"`, `"too_many_members"` (more than `max_members`
 * elements), `"trailing_data"` (non-whitespace after the closing `]`), or
 * `"malformed"` (any other syntax error, including a trailing comma).
 */
array_scan_result scan_array(std::string_view text,
                             const scan_limits& limits = {});

} // namespace url_shortener::commands
