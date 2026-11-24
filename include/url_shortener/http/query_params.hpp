#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace url_shortener
{
namespace http
{

/**
 * @brief One decoded `name=value` pair from a request query string.
 *
 * Both fields are already percent-decoded (`%XX` -> byte, `+` -> space). A
 * parameter written without `=` (e.g. `a`) yields an empty @ref value; the
 * grammar of the name is **not** checked here (the caller validates it), only
 * that it is non-empty.
 */
struct query_param
{
    ///< Decoded parameter name; guaranteed non-empty by @ref
    ///< parse_query_string.
    std::string name;

    ///< Decoded parameter value; empty for a bare `name` with no `=`.
    std::string value;
};

/**
 * @brief Percent-decodes a single query-string token (name or value).
 *
 * Decodes `+` to a space and `%XX` (two ASCII hex digits, case-insensitive) to
 * the corresponding byte; every other byte is copied verbatim. Rejects a
 * malformed escape and a decoded NUL so a truncating byte can never reach the
 * command layer (threat model T-73).
 *
 * @param in The raw (still-encoded) token; may be empty.
 * @return The decoded string on success; `std::nullopt` when @p in contains a
 * malformed `%` escape (`%` not followed by two hex digits, including a
 * truncated `%2` or a trailing `%`) or a `%00` that decodes to a NUL byte.
 */
std::optional<std::string> percent_decode(std::string_view in);

/**
 * @brief Parses a request query string into decoded `name=value` pairs.
 *
 * Splits @p query (which must already have the leading `?` removed) on `&`, then
 * each token on its first `=`, and percent-decodes both sides via
 * @ref percent_decode. Duplicate names are **preserved in order** — detecting
 * them is the caller's decision. The parser is iterative and allocates at most
 * the size of @p query, which the request-target length limit (2048 bytes by
 * default) already bounds, so it needs no internal cap (threat model T-68).
 *
 * @param query The raw query string without the leading `?`; an empty string
 * yields zero parameters and success.
 * @param out Receives the decoded parameters in order; cleared first. Must be
 * non-null.
 * @param error Receives the fixed token `"malformed_query"` when the function
 * returns `false`; untouched on success. Must be non-null.
 * @return `true` and fills @p out on success; `false` with `*error ==
 * "malformed_query"` when a token has an empty name (`=x`, `&&`, a leading or
 * trailing `&`) or either side fails @ref percent_decode (malformed `%XX` or a
 * decoded NUL).
 */
bool parse_query_string(std::string_view query,
                        std::vector<query_param>* out,
                        std::string* error);

}  // namespace http
}  // namespace url_shortener
