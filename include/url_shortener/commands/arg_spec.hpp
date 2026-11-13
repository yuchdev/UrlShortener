#pragma once

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/error_codes.hpp>
#include <url_shortener/commands/json_scan.hpp>

namespace url_shortener::commands
{

/**
 * @brief The catalog argument types an @ref ArgSpec can describe.
 *
 * This is the single schema vocabulary that later drives CLI flag compilation,
 * TUI form generation and the docs catalog (ADR 0002 section 2). It is purely
 * descriptive: it names a shape and the validation applied to it, and knows
 * nothing about any transport. Enumerator order is not contractual.
 */
enum class ArgType
{
    ///< A JSON string value, decoded and length-capped at `max_length`.
    string,

    ///< A JSON integer token (no fraction/exponent) that fits in `long long`
    ///< and lies within `[min, max]` when those bounds are set.
    integer,

    ///< A JSON boolean literal (`true`/`false`).
    boolean,

    ///< A JSON string equal, case-sensitively, to one of `enum_values`.
    enumeration,

    ///< A JSON string accepted by `url_shortener::parseRfc3339Zulu`
    ///< (strict `%Y-%m-%dT%H:%M:%SZ`).
    timestamp,

    ///< A JSON array of strings, at most `max_items` entries, each decoded
    ///< value at most `max_length` bytes.
    string_list,

    ///< A flat JSON object whose values are all strings, at most `max_items`
    ///< entries, each key and value at most `max_length` bytes.
    string_map,

    ///< A nested JSON object whose members must all be declared in `fields`;
    ///< every member is validated recursively (strings only in v1).
    object
};

/**
 * @brief Declarative schema for a single command argument.
 *
 * An `ArgSpec` describes one argument's name, type, presence rules and bounds.
 * A `std::vector<ArgSpec>` fully specifies the `args` object of one command and
 * is the single definition consumed by @ref validate_args (milestone contract
 * C1). It carries no transport- or storage-specific behavior; domain rules
 * (URL validity, slug syntax, SSRF checks) are applied later in
 * `LinkCommandService`, never here.
 */
struct ArgSpec
{
    ///< Argument key as it appears in the `args` object. Unique within one
    ///< command's spec set.
    std::string name;

    ///< The argument's catalog type; selects which validation rules apply.
    ArgType type = ArgType::string;

    ///< `true` when the argument must be present; a missing required argument
    ///< fails with reason `missing`.
    bool required = false;

    ///< `true` when an explicit JSON `null` is accepted (PATCH-style clear).
    ///< When `false`, a `null` value fails with reason `wrong_type`. Absence
    ///< and explicit null remain distinguishable via @ref ArgsView::has and
    ///< @ref ArgsView::is_null.
    bool nullable = false;

    ///< Allowed values for `ArgType::enumeration`, compared case-sensitively.
    ///< Unused for every other type.
    std::vector<std::string> enum_values;

    ///< Raw JSON token documenting the default applied by the handler when the
    ///< argument is omitted. Documentation and help only; @ref validate_args
    ///< never reads it and never mutates `args`.
    std::optional<std::string> default_value;

    ///< One-line human description for the generated catalog and help.
    std::string doc;

    ///< Inclusive lower bound for `ArgType::integer`; unset means unbounded
    ///< below. A value below it fails with reason `out_of_range`.
    std::optional<long long> min;

    ///< Inclusive upper bound for `ArgType::integer`; unset means unbounded
    ///< above. A value above it fails with reason `out_of_range`.
    std::optional<long long> max;

    ///< Maximum decoded byte length of a string value, each `string_list`
    ///< item, and each `string_map` key and value. Exceeding it fails with
    ///< reason `too_long`. Also the per-arg nested string/key byte cap handed
    ///< to the scanner when re-scanning a list/map/object value.
    std::size_t max_length = 2048;

    ///< Maximum number of `string_list` entries, `string_map` entries, or
    ///< nested `object` members. Exceeding it fails with reason
    ///< `too_many_items`. Also the per-arg nested `max_members` handed to the
    ///< scanner so a raised cap (decision D10: up to 4096) accepts what REST
    ///< accepts without relaxing the top-level envelope DoS caps.
    std::size_t max_items = 64;

    ///< Member specs for `ArgType::object`; each nested member must match one
    ///< of these by name (all optional strings in v1). An undeclared nested
    ///< member fails with reason `unknown`.
    std::vector<ArgSpec> fields;

    ///< Non-empty group tag for mutually-exclusive arguments: among all specs
    ///< sharing one non-empty group, exactly one must be present, else the
    ///< group fails with reason `one_of_violation` (e.g. `link.get`
    ///< `slug` vs `id`). Empty means the argument is not part of any group.
    std::string one_of_group;
};

/**
 * @brief Read-only, validated accessor over one `args` object's members.
 *
 * Populated by @ref validate_args on success, an `ArgsView` offers typed,
 * decoded reads of the arguments that already passed their @ref ArgSpec. It is
 * a **non-owning** view in spirit: it holds the scanned top-level members and
 * decodes leaf values on demand; a handler that outlives the request buffer
 * must copy every value it keeps. Accessors return `std::nullopt` for an absent
 * or JSON-null argument and never throw.
 */
class ArgsView
{
public:
    /**
     * @brief Tests whether an argument is present (including explicit null).
     *
     * @param name Argument key to look up.
     * @return `true` when a member with key @p name exists, whether its value
     * is a real value or an explicit JSON `null`; `false` when absent.
     */
    bool has(std::string_view name) const;

    /**
     * @brief Tests whether an argument is present and an explicit JSON null.
     *
     * @param name Argument key to look up.
     * @return `true` when a member with key @p name exists and its value is the
     * JSON literal `null`; `false` when absent or a non-null value.
     */
    bool is_null(std::string_view name) const;

    /**
     * @brief Reads a string-typed argument, decoded from its JSON token.
     *
     * @param name Argument key to look up.
     * @return The decoded UTF-8 string when the argument is present with a
     * string value; `std::nullopt` when absent, JSON null, a non-string value,
     * or the token fails to decode.
     */
    std::optional<std::string> get_string(std::string_view name) const;

    /**
     * @brief Reads an integer-typed argument.
     *
     * @param name Argument key to look up.
     * @return The parsed value when the argument is present as a JSON integer
     * token fitting `long long`; `std::nullopt` when absent, JSON null, a
     * non-number, a fractional/exponent number, or out of `long long` range.
     */
    std::optional<long long> get_int(std::string_view name) const;

    /**
     * @brief Reads a boolean-typed argument.
     *
     * @param name Argument key to look up.
     * @return `true`/`false` when the argument is present as a JSON boolean;
     * `std::nullopt` when absent, JSON null, or a non-boolean value.
     */
    std::optional<bool> get_bool(std::string_view name) const;

    /**
     * @brief Reads a `string_list` argument as decoded strings.
     *
     * @param name Argument key to look up.
     * @return The decoded elements in source order when the argument is present
     * as an array of strings; `std::nullopt` when absent, JSON null, a
     * non-array, or any element is not a decodable string.
     */
    std::optional<std::vector<std::string>> get_string_list(
        std::string_view name) const;

    /**
     * @brief Reads a `string_map` argument as decoded key/value pairs.
     *
     * @param name Argument key to look up.
     * @return The decoded key/value pairs when the argument is present as a
     * flat object of string values; `std::nullopt` when absent, JSON null, a
     * non-object, or any value is not a decodable string.
     */
    std::optional<std::map<std::string, std::string>> get_string_map(
        std::string_view name) const;

    /**
     * @brief Reads a nested `object` argument as its own view.
     *
     * @param name Argument key to look up.
     * @return An @ref ArgsView over the nested object's members when the
     * argument is present as an object; `std::nullopt` when absent, JSON null,
     * or a non-object value.
     */
    std::optional<ArgsView> get_object(std::string_view name) const;

    /**
     * @brief Returns the raw, undecoded JSON source text of an argument.
     *
     * @param name Argument key to look up.
     * @return The exact source token (including quotes for strings, brace/
     * bracket to brace/bracket for containers) when present; the empty string
     * when absent.
     */
    std::string raw(std::string_view name) const;

private:
    ///< The scanned top-level members of the `args` object (or a nested
    ///< object), in source order. Keys are decoded; values are raw tokens.
    std::vector<json_member> m_members;

    /**
     * @brief Grants @ref validate_args write access to populate @ref m_members.
     */
    friend bool validate_args(const std::vector<ArgSpec>& specs,
                              std::string_view args_json, ArgsView* out,
                              CommandError* error);
};

/**
 * @brief Validates one `args` JSON object against a spec set.
 *
 * Scans @p args_json with the one sanctioned scanner, then checks every member
 * against @p specs: unknown keys, missing required args, per-type shape and
 * bounds, and the `one_of_group` constraint. No domain rule (URL validity, slug
 * syntax, SSRF) is applied here. Defaults are never materialized; @p args_json
 * is never mutated.
 *
 * Security properties: the top-level args object is scanned with the
 * request-hardening defaults for member count, so the top-level DoS caps are
 * preserved; each `string_list`/`string_map`/`object` value is re-scanned with
 * a per-arg `scan_limits` derived from that `ArgSpec` (`max_items` ->
 * `max_members`, `max_length` -> the key/value byte cap), so a raised per-arg
 * cap (decision D10) accepts what REST accepts without relaxing the top-level
 * caps (threat model T-06). Every decoded string value (and list element) is
 * rejected if it contains an embedded NUL (threat model T-03, reason
 * `wrong_type`); NUL in a key is rejected by the scanner before enumeration.
 * Every failure message is routed through @ref sanitize_error_message and names
 * only a fixed reason and, at most, an argument *name* (truncated to 64 bytes
 * and control-stripped), never an argument *value* (contract C6, threat model
 * T-02).
 *
 * @param specs The argument schema for one command; names should be unique.
 * @param args_json Exact JSON source text of the `args` object (untrusted;
 * never logged by this function).
 * @param out When non-null and validation succeeds, populated with the scanned
 * members for typed reads; left unchanged on failure.
 * @param error When non-null and validation fails, set to `invalid_field` with
 * message `"<arg-name>: <reason>"` (or `"args: <reason>"` when the object
 * itself is malformed), where `<reason>` is one of the fixed vocabulary:
 * `unknown`, `missing`, `wrong_type`, `out_of_range`, `not_in_enum`,
 * `too_long`, `too_many_items`, `invalid_timestamp`, `one_of_violation`; left
 * unchanged on success.
 * @return `true` when every argument satisfies its spec; `false` on the first
 * violation (object not a valid JSON object, over the top-level member/byte/
 * depth caps, an unknown key, a missing required arg, a wrong-typed/out-of-
 * range/over-length/over-count value, a bad enum/timestamp, a NUL in a decoded
 * value, or a `one_of_group` with not exactly one member present).
 */
bool validate_args(const std::vector<ArgSpec>& specs,
                   std::string_view args_json, ArgsView* out,
                   CommandError* error);

}  // namespace url_shortener::commands
