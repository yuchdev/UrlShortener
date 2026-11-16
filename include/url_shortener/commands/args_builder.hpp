#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace url_shortener::commands
{

/**
 * @brief Assembles the flat JSON object text carried by
 * `CommandRequest::args_json`, member by member, in insertion order.
 *
 * This is the one helper both transports use to turn native input (REST query
 * string and body fields, the CLI `cmd --arg k=v` path, the `link <verb>` flag
 * compiler) into the `args` object, so the JSON escaping and key rules exist in
 * exactly one place and no adapter hand-builds `args` text (milestone C9: no
 * JSON library). The produced text is always a single well-formed JSON object:
 * `"{}"` when empty, otherwise `{"a":..,"b":..}` with members in the order they
 * were added (`add_string_map` emits its own keys sorted).
 *
 * Every string value is escaped through the shared `url_shortener::jsonString`
 * escaper in the implementation; callers never concatenate untrusted input.
 * The only unescaped path is `add_raw`, which is for JSON tokens produced by an
 * already-validated source (e.g. a token the REST layer has just scanned); see
 * its contract.
 *
 * @note Member names are validated against `[a-z][a-z0-9_]*` (all catalog
 * argument names match this). A malformed or duplicate name is a programming
 * error, not a user-input error - builders are fed with compile-time literals -
 * so it throws `std::invalid_argument` rather than returning a status. The
 * thrown message names only the offending key, never a value.
 *
 * @note No allocation limits are enforced here beyond the caller's: the REST
 * and CLI callers bound their input size (request-body hardening / argv size)
 * before building, so a value may legitimately be large.
 */
class args_builder
{
public:
    /**
     * @brief Appends a string member whose value is JSON-escaped.
     *
     * @param name Member name; must match `[a-z][a-z0-9_]*` and not already be
     * present.
     * @param value Arbitrary (possibly untrusted) text; escaped via
     * `jsonString` so quotes, backslashes, control characters and invalid
     * UTF-8 bytes are emitted safely.
     * @return `*this`, to allow chaining.
     * @throw std::invalid_argument if @p name is malformed or duplicates an
     * already-added member.
     */
    args_builder& add_string(std::string_view name, std::string_view value);

    /**
     * @brief Appends a boolean member emitted as the literal `true`/`false`.
     *
     * @param name Member name; must match `[a-z][a-z0-9_]*` and be unique.
     * @param value Boolean to emit.
     * @return `*this`, to allow chaining.
     * @throw std::invalid_argument if @p name is malformed or duplicates an
     * already-added member.
     */
    args_builder& add_bool(std::string_view name, bool value);

    /**
     * @brief Appends an integer member emitted as a JSON number.
     *
     * @param name Member name; must match `[a-z][a-z0-9_]*` and be unique.
     * @param value Signed 64-bit integer; rendered in base 10.
     * @return `*this`, to allow chaining.
     * @throw std::invalid_argument if @p name is malformed or duplicates an
     * already-added member.
     */
    args_builder& add_int(std::string_view name, long long value);

    /**
     * @brief Appends a member whose value is the JSON `null` literal.
     *
     * @param name Member name; must match `[a-z][a-z0-9_]*` and be unique.
     * @return `*this`, to allow chaining.
     * @throw std::invalid_argument if @p name is malformed or duplicates an
     * already-added member.
     */
    args_builder& add_null(std::string_view name);

    /**
     * @brief Appends a member whose value is inserted verbatim, without
     * escaping or validation.
     *
     * @warning @p json_token must already be exactly one valid JSON value
     * (object, array, number, ...). It is inserted as-is; this builder does not
     * parse or validate it. Use only for tokens from an already-validated
     * source - e.g. a nested object built by a second `args_builder`, or a
     * token the REST layer has just scanned with the 01.0 scanner. Never pass
     * untrusted text here; use `add_string` instead.
     *
     * @param name Member name; must match `[a-z][a-z0-9_]*` and be unique.
     * @param json_token A single, already-valid JSON value.
     * @return `*this`, to allow chaining.
     * @throw std::invalid_argument if @p name is malformed or duplicates an
     * already-added member.
     */
    args_builder& add_raw(std::string_view name, std::string_view json_token);

    /**
     * @brief Appends a member whose value is a JSON array of escaped strings.
     *
     * Emits `[]` for an empty list; each element is escaped via `jsonString`.
     *
     * @param name Member name; must match `[a-z][a-z0-9_]*` and be unique.
     * @param values Elements in order; each escaped individually.
     * @return `*this`, to allow chaining.
     * @throw std::invalid_argument if @p name is malformed or duplicates an
     * already-added member.
     */
    args_builder& add_string_list(std::string_view name,
                                  const std::vector<std::string>& values);

    /**
     * @brief Appends a member whose value is a JSON object of string->string
     * entries, with keys emitted in sorted order.
     *
     * Keys are emitted in the sorted order guaranteed by `std::map`, giving
     * deterministic output text (matching the sorted metadata of
     * `serializeLinkViewJson`). Both the map keys and the map values are
     * escaped via `jsonString`, so hostile map keys cannot break the enclosing
     * object. Emits `{}` for an empty map.
     *
     * @param name Member name; must match `[a-z][a-z0-9_]*` and be unique.
     * @param values Entries to emit; the `std::map` ordering fixes key order.
     * @return `*this`, to allow chaining.
     * @throw std::invalid_argument if @p name is malformed or duplicates an
     * already-added member.
     */
    args_builder& add_string_map(std::string_view name,
                                 const std::map<std::string, std::string>& values);

    /**
     * @brief Whether no member has been added yet.
     * @return `true` when `str()` would return `"{}"`.
     */
    bool empty() const;

    /**
     * @brief Whether a member with the given name has been added.
     *
     * @param name Member name to look up; compared by value. Not validated
     * against the name grammar (an invalid name simply never matches).
     * @return `true` if a member with that exact name is present.
     */
    bool has(std::string_view name) const;

    /**
     * @brief Renders the accumulated members as one JSON object text.
     *
     * @return `"{}"` when empty, otherwise `{"name":value,...}` with members in
     * insertion order. The result is always a single well-formed JSON object
     * parseable by `scan_object`.
     */
    std::string str() const;

private:
    /// Accumulated members as `(name, already-serialized value)` pairs, in
    /// insertion order. The name is stored raw; the value is already-escaped
    /// JSON text ready to emit between the `:` and the following `,`/`}`.
    std::vector<std::pair<std::string, std::string>> m_members;
};

}  // namespace url_shortener::commands
