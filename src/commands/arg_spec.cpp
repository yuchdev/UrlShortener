#include "url_shortener/commands/arg_spec.hpp"

#include <cstddef>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "url_shortener/core/utils.hpp"

namespace url_shortener::commands
{

namespace
{

// Fixed reason vocabulary appended after an argument name in a failure
// message. These are the only reason words `validate_args` emits; they carry
// no argument value, so a message built from `"<name>: <reason>"` is safe to
// log and return (contract C6, threat model T-02).
constexpr std::string_view k_unknown = "unknown";
constexpr std::string_view k_missing = "missing";
constexpr std::string_view k_wrong_type = "wrong_type";
constexpr std::string_view k_out_of_range = "out_of_range";
constexpr std::string_view k_not_in_enum = "not_in_enum";
constexpr std::string_view k_too_long = "too_long";
constexpr std::string_view k_too_many_items = "too_many_items";
constexpr std::string_view k_invalid_timestamp = "invalid_timestamp";
constexpr std::string_view k_one_of_violation = "one_of_violation";

// Maximum bytes of an argument name echoed in a message (threat model T-02).
constexpr std::size_t k_max_name_in_message = 64;

/**
 * @brief Builds an `invalid_field` error naming an argument and a reason.
 *
 * The name is truncated to at most @ref k_max_name_in_message bytes and the
 * whole message is routed through @ref sanitize_error_message, so any control
 * characters or invalid UTF-8 in an attacker-supplied (unknown) key are
 * neutralized and the message can never carry an argument value.
 *
 * @param name   Argument name (or `"args"` / a `one_of_group` label); never a
 * value.
 * @param reason One of the fixed reason words above.
 * @return A @ref CommandError with code `invalid_field` and a sanitized
 * `"<name>: <reason>"` message.
 */
CommandError fail(std::string_view name, std::string_view reason)
{
    if (name.size() > k_max_name_in_message)
    {
        name = name.substr(0, k_max_name_in_message);
    }
    std::string message(name);
    message += ": ";
    message += reason;
    return make_error(CommandErrorCode::invalid_field,
                      sanitize_error_message(message));
}

/**
 * @brief Finds the spec whose name equals @p key.
 *
 * @param specs Spec set to search (small; linear scan is adequate).
 * @param key   Decoded member key to match.
 * @return Pointer to the matching spec, or `nullptr` if none matches.
 */
const ArgSpec* find_spec(const std::vector<ArgSpec>& specs,
                         std::string_view key)
{
    for (const ArgSpec& spec : specs)
    {
        if (spec.name == key)
        {
            return &spec;
        }
    }
    return nullptr;
}

/**
 * @brief Derives a per-arg `scan_limits` for a nested list/map/object value.
 *
 * `max_items` becomes the nested member cap and `max_length` the nested key
 * byte cap, so a raised per-arg cap (decision D10) accepts what REST accepts
 * without relaxing the top-level envelope DoS caps (threat model T-06). The
 * byte cap is the value's own size (already bounded by the parent scan).
 *
 * @param spec The argument whose value is being re-scanned.
 * @param raw  The exact source text of that value.
 * @return Limits for the nested `scan_array`/`scan_object`.
 */
scan_limits nested_limits(const ArgSpec& spec, std::string_view raw)
{
    scan_limits limits;
    limits.max_bytes = raw.size();
    limits.max_depth = 8;
    limits.max_members = spec.max_items;
    limits.max_key_len = spec.max_length;
    return limits;
}

/**
 * @brief Permissive `scan_limits` for an @ref ArgsView accessor re-scan.
 *
 * The value was already validated when the view was produced, so the only goal
 * is to re-split it without a cap rejecting already-accepted content; every
 * bound is tied to the value's own size.
 *
 * @param raw The exact source text being re-scanned.
 * @return Limits that always admit @p raw.
 */
scan_limits permissive_limits(std::string_view raw)
{
    scan_limits limits;
    limits.max_bytes = raw.size();
    limits.max_depth = 16;
    limits.max_members = raw.size();
    limits.max_key_len = raw.size();
    return limits;
}

/**
 * @brief Outcome of parsing a JSON number token as a `long long` integer.
 */
enum class int_parse
{
    ///< Parsed to a value that fits in `long long`.
    ok,

    ///< The token is a number but not an integer (has a fraction or exponent).
    not_integer,

    ///< The token is an integer but does not fit in `long long`.
    overflow
};

/**
 * @brief Parses a JSON number token as a signed integer.
 *
 * Rejects any fraction/exponent form (`1.0`, `1e2`) as @ref
 * int_parse::not_integer and any magnitude outside `long long` as @ref
 * int_parse::overflow (threat model T-07). Assumes @p token is a scanner-
 * validated JSON number (`json_kind::number`) but re-checks each byte
 * defensively.
 *
 * @param token The raw number source text (no surrounding quotes).
 * @param out   Set to the parsed value only when the result is @ref
 * int_parse::ok.
 * @return The parse outcome.
 */
int_parse parse_integer_token(std::string_view token, long long& out)
{
    if (token.empty())
    {
        return int_parse::not_integer;
    }
    for (const char c : token)
    {
        if (c == '.' || c == 'e' || c == 'E')
        {
            return int_parse::not_integer;
        }
    }

    std::size_t i = 0;
    bool negative = false;
    if (token[i] == '-')
    {
        negative = true;
        ++i;
    }
    if (i >= token.size())
    {
        return int_parse::not_integer;
    }

    // Accumulate the magnitude as unsigned so LLONG_MIN is representable; the
    // positive limit is LLONG_MAX, the negative limit is LLONG_MAX + 1.
    const unsigned long long limit =
        negative ? static_cast<unsigned long long>(
                       std::numeric_limits<long long>::max()) +
                       1ULL
                 : static_cast<unsigned long long>(
                       std::numeric_limits<long long>::max());
    unsigned long long acc = 0;
    for (; i < token.size(); ++i)
    {
        const char c = token[i];
        if (c < '0' || c > '9')
        {
            return int_parse::not_integer;
        }
        const unsigned long long digit = static_cast<unsigned long long>(c -
                                                                         '0');
        if (acc > limit / 10ULL ||
            (acc == limit / 10ULL && digit > limit % 10ULL))
        {
            return int_parse::overflow;
        }
        acc = acc * 10ULL + digit;
    }

    out = negative ? static_cast<long long>(~acc + 1ULL)
                   : static_cast<long long>(acc);
    return int_parse::ok;
}

/**
 * @brief Decodes a string token and rejects an embedded NUL.
 *
 * @param raw_token The exact JSON string source (including quotes).
 * @param out       Set to the decoded UTF-8 string on success.
 * @return `true` when the token decodes and contains no embedded NUL;
 * `false` otherwise (undecodable, or a `\u0000` that decoded to a NUL byte;
 * threat model T-03).
 */
bool decode_without_nul(std::string_view raw_token, std::string& out)
{
    std::optional<std::string> decoded = decode_json_string(raw_token);
    if (!decoded || decoded->find('\0') != std::string::npos)
    {
        return false;
    }
    out = std::move(*decoded);
    return true;
}

// Forward declaration: object-typed args recurse back into this routine.
bool check_against_specs(const std::vector<ArgSpec>& specs,
                         const std::vector<json_member>& members,
                         CommandError* error, std::string_view prefix);

/**
 * @brief Validates one present member's value against its matched spec.
 *
 * @param spec  The spec the member matched by name.
 * @param member The scanned member (key + kind + raw value).
 * @param name  The display name used in any failure message (prefix + key).
 * @param error Set to the first per-type violation when non-null.
 * @return `true` when the value satisfies the spec; `false` on the first
 * violation.
 */
bool validate_member(const ArgSpec& spec, const json_member& member,
                     std::string_view name, CommandError* error)
{
    const auto reject = [&](std::string_view reason) {
        if (error != nullptr)
        {
            *error = fail(name, reason);
        }
        return false;
    };

    if (member.kind == json_kind::null)
    {
        return spec.nullable ? true : reject(k_wrong_type);
    }

    switch (spec.type)
    {
    case ArgType::string:
    {
        if (member.kind != json_kind::string)
        {
            return reject(k_wrong_type);
        }
        std::string value;
        if (!decode_without_nul(member.raw, value))
        {
            return reject(k_wrong_type);
        }
        if (value.size() > spec.max_length)
        {
            return reject(k_too_long);
        }
        return true;
    }
    case ArgType::enumeration:
    {
        if (member.kind != json_kind::string)
        {
            return reject(k_wrong_type);
        }
        std::string value;
        if (!decode_without_nul(member.raw, value))
        {
            return reject(k_wrong_type);
        }
        for (const std::string& allowed : spec.enum_values)
        {
            if (allowed == value)
            {
                return true;
            }
        }
        return reject(k_not_in_enum);
    }
    case ArgType::timestamp:
    {
        if (member.kind != json_kind::string)
        {
            return reject(k_invalid_timestamp);
        }
        std::string value;
        if (!decode_without_nul(member.raw, value))
        {
            return reject(k_invalid_timestamp);
        }
        if (!url_shortener::parseRfc3339Zulu(value).has_value())
        {
            return reject(k_invalid_timestamp);
        }
        return true;
    }
    case ArgType::integer:
    {
        if (member.kind != json_kind::number)
        {
            return reject(k_wrong_type);
        }
        long long value = 0;
        switch (parse_integer_token(member.raw, value))
        {
        case int_parse::not_integer:
            return reject(k_wrong_type);
        case int_parse::overflow:
            return reject(k_out_of_range);
        case int_parse::ok:
            break;
        }
        if (spec.min.has_value() && value < *spec.min)
        {
            return reject(k_out_of_range);
        }
        if (spec.max.has_value() && value > *spec.max)
        {
            return reject(k_out_of_range);
        }
        return true;
    }
    case ArgType::boolean:
    {
        return member.kind == json_kind::boolean ? true
                                                 : reject(k_wrong_type);
    }
    case ArgType::string_list:
    {
        if (member.kind != json_kind::array)
        {
            return reject(k_wrong_type);
        }
        const array_scan_result scan =
            scan_array(member.raw, nested_limits(spec, member.raw));
        if (!scan.ok)
        {
            return reject(scan.error == "too_many_members" ? k_too_many_items
                                                           : k_wrong_type);
        }
        for (const json_element& element : scan.elements)
        {
            if (element.kind != json_kind::string)
            {
                return reject(k_wrong_type);
            }
            std::string value;
            if (!decode_without_nul(element.raw, value))
            {
                return reject(k_wrong_type);
            }
            if (value.size() > spec.max_length)
            {
                return reject(k_too_long);
            }
        }
        return true;
    }
    case ArgType::string_map:
    {
        if (member.kind != json_kind::object)
        {
            return reject(k_wrong_type);
        }
        const scan_result scan =
            scan_object(member.raw, nested_limits(spec, member.raw));
        if (!scan.ok)
        {
            return reject(scan.error == "too_many_members" ? k_too_many_items
                                                           : k_wrong_type);
        }
        for (const json_member& entry : scan.members)
        {
            if (entry.kind != json_kind::string)
            {
                return reject(k_wrong_type);
            }
            std::string value;
            if (!decode_without_nul(entry.raw, value))
            {
                return reject(k_wrong_type);
            }
            if (value.size() > spec.max_length)
            {
                return reject(k_too_long);
            }
        }
        return true;
    }
    case ArgType::object:
    {
        if (member.kind != json_kind::object)
        {
            return reject(k_wrong_type);
        }
        const scan_result scan =
            scan_object(member.raw, nested_limits(spec, member.raw));
        if (!scan.ok)
        {
            return reject(scan.error == "too_many_members" ? k_too_many_items
                                                           : k_wrong_type);
        }
        std::string prefix(name);
        prefix += '.';
        return check_against_specs(spec.fields, scan.members, error, prefix);
    }
    }
    return reject(k_wrong_type);
}

bool check_against_specs(const std::vector<ArgSpec>& specs,
                         const std::vector<json_member>& members,
                         CommandError* error, std::string_view prefix)
{
    // Per-member: reject unknown keys, then validate each matched value.
    for (const json_member& member : members)
    {
        const ArgSpec* spec = find_spec(specs, member.key);
        std::string name(prefix);
        name += member.key;
        if (spec == nullptr)
        {
            if (error != nullptr)
            {
                *error = fail(name, k_unknown);
            }
            return false;
        }
        if (!validate_member(*spec, member, name, error))
        {
            return false;
        }
    }

    // Required-presence pass.
    for (const ArgSpec& spec : specs)
    {
        if (spec.required &&
            find_member(members, spec.name) == nullptr)
        {
            if (error != nullptr)
            {
                std::string name(prefix);
                name += spec.name;
                *error = fail(name, k_missing);
            }
            return false;
        }
    }

    // one_of_group pass: among specs sharing a non-empty group, exactly one
    // must be present. Each group is evaluated once, at its first spec.
    for (std::size_t i = 0; i < specs.size(); ++i)
    {
        const std::string& group = specs[i].one_of_group;
        if (group.empty())
        {
            continue;
        }
        bool first = true;
        for (std::size_t j = 0; j < i; ++j)
        {
            if (specs[j].one_of_group == group)
            {
                first = false;
                break;
            }
        }
        if (!first)
        {
            continue;
        }
        std::size_t present = 0;
        for (const ArgSpec& spec : specs)
        {
            if (spec.one_of_group == group &&
                find_member(members, spec.name) != nullptr)
            {
                ++present;
            }
        }
        if (present != 1)
        {
            if (error != nullptr)
            {
                *error = fail(group, k_one_of_violation);
            }
            return false;
        }
    }

    return true;
}

}  // namespace

bool ArgsView::has(std::string_view name) const
{
    return find_member(m_members, name) != nullptr;
}

bool ArgsView::is_null(std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    return member != nullptr && member->kind == json_kind::null;
}

std::optional<std::string> ArgsView::get_string(std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    if (member == nullptr || member->kind != json_kind::string)
    {
        return std::nullopt;
    }
    return decode_json_string(member->raw);
}

std::optional<long long> ArgsView::get_int(std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    if (member == nullptr || member->kind != json_kind::number)
    {
        return std::nullopt;
    }
    long long value = 0;
    if (parse_integer_token(member->raw, value) != int_parse::ok)
    {
        return std::nullopt;
    }
    return value;
}

std::optional<bool> ArgsView::get_bool(std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    if (member == nullptr || member->kind != json_kind::boolean)
    {
        return std::nullopt;
    }
    return member->raw == "true";
}

std::optional<std::vector<std::string>> ArgsView::get_string_list(
    std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    if (member == nullptr || member->kind != json_kind::array)
    {
        return std::nullopt;
    }
    const array_scan_result scan =
        scan_array(member->raw, permissive_limits(member->raw));
    if (!scan.ok)
    {
        return std::nullopt;
    }
    std::vector<std::string> values;
    values.reserve(scan.elements.size());
    for (const json_element& element : scan.elements)
    {
        if (element.kind != json_kind::string)
        {
            return std::nullopt;
        }
        std::optional<std::string> decoded = decode_json_string(element.raw);
        if (!decoded)
        {
            return std::nullopt;
        }
        values.push_back(std::move(*decoded));
    }
    return values;
}

std::optional<std::map<std::string, std::string>> ArgsView::get_string_map(
    std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    if (member == nullptr || member->kind != json_kind::object)
    {
        return std::nullopt;
    }
    const scan_result scan =
        scan_object(member->raw, permissive_limits(member->raw));
    if (!scan.ok)
    {
        return std::nullopt;
    }
    std::map<std::string, std::string> values;
    for (const json_member& entry : scan.members)
    {
        if (entry.kind != json_kind::string)
        {
            return std::nullopt;
        }
        std::optional<std::string> decoded = decode_json_string(entry.raw);
        if (!decoded)
        {
            return std::nullopt;
        }
        values.emplace(entry.key, std::move(*decoded));
    }
    return values;
}

std::optional<ArgsView> ArgsView::get_object(std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    if (member == nullptr || member->kind != json_kind::object)
    {
        return std::nullopt;
    }
    const scan_result scan =
        scan_object(member->raw, permissive_limits(member->raw));
    if (!scan.ok)
    {
        return std::nullopt;
    }
    ArgsView view;
    view.m_members = scan.members;
    return view;
}

std::string ArgsView::raw(std::string_view name) const
{
    const json_member* member = find_member(m_members, name);
    return member != nullptr ? member->raw : std::string();
}

bool validate_args(const std::vector<ArgSpec>& specs,
                   std::string_view args_json, ArgsView* out,
                   CommandError* error)
{
    // Top-level args scan keeps the request-hardening member/key/depth caps so
    // the envelope DoS caps are not relaxed (threat model T-06). The byte cap
    // is the args text's own size: byte-level DoS is enforced upstream by the
    // envelope scan (subtask 03), not re-imposed here where a legitimately
    // large (but already-bounded) args object must still validate.
    scan_limits top;
    top.max_bytes = args_json.size();
    top.max_depth = 8;
    top.max_members = 256;
    top.max_key_len = 128;

    scan_result scan = scan_object(args_json, top);
    if (!scan.ok)
    {
        if (error != nullptr)
        {
            // scan.error is a fixed, value-free scanner vocabulary word.
            *error = fail("args", scan.error);
        }
        return false;
    }

    if (!check_against_specs(specs, scan.members, error, ""))
    {
        return false;
    }

    if (out != nullptr)
    {
        out->m_members = std::move(scan.members);
    }
    return true;
}

}  // namespace url_shortener::commands
