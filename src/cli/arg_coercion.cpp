#include <url_shortener/cli/arg_coercion.hpp>

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/args_builder.hpp>

namespace url_shortener::cli
{

namespace
{

using commands::ArgSpec;
using commands::ArgType;

/**
 * @brief Whether @p name matches the catalog argument-name grammar
 * `[a-z][a-z0-9_]*`.
 *
 * Mirrors `args_builder`'s own `is_valid_name`: every catalog argument name
 * matches this, and `args_builder::add_*` throws on a name outside it. Validating
 * each name (and each dotted segment) here first turns a hostile name into a
 * `bad_name` status instead of letting it reach - and throw inside - the builder
 * (threat model T-78).
 *
 * @param name Candidate name or dotted segment.
 * @return `true` when @p name is non-empty, begins with `a-z`, and otherwise
 * contains only `a-z`, `0-9` or `_`; `false` otherwise.
 */
bool is_valid_name(std::string_view name)
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
 * @brief Canonicalizes an all-digits value into a JSON integer token, or reports
 * that it is not a plain integer.
 *
 * Replicates the REST query coercion `canonical_integer_token`
 * (`src/http/handlers/link_handlers.cpp`) byte-for-byte so CLI and REST agree on
 * which `--arg`/query value is an integer: a value is a JSON number only when it
 * is `^[0-9]{1,18}$` (at most 18 digits, so it always fits `long long`), with
 * leading zeros stripped and a single `0` kept for an all-zero input (`007` ->
 * `7`, `00` -> `0`, decimal, never octal). Anything else - a sign, a non-digit,
 * empty, a fraction, or 19+ digits - returns `std::nullopt`, and the caller emits
 * the raw value as a JSON string so the registry rejects it as `wrong_type`.
 *
 * The REST copy lives in an anonymous namespace inside a contract-C3-protected
 * handler; extracting it is outside this subtask's file set, so the identical
 * rule is replicated here rather than shared.
 *
 * @param value Decoded argument value.
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

/**
 * @brief Finds the @ref ArgSpec declaring @p name, if any.
 *
 * @param specs The target command's argument schema.
 * @param name Plain argument name to look up (compared by value).
 * @return A pointer to the matching spec, or `nullptr` when no spec declares
 * @p name (an unknown arg, or an empty @p specs). The pointer is valid only
 * while @p specs is unmodified and alive.
 */
const ArgSpec* find_spec(const std::vector<ArgSpec>& specs,
                         std::string_view name)
{
    for (const ArgSpec& spec : specs) {
        if (spec.name == name) {
            return &spec;
        }
    }
    return nullptr;
}

/**
 * @brief Whether @p spec declares a dotted-path container (`string_map` or
 * `object`).
 *
 * @param spec Spec to classify; may be `nullptr` (unknown arg).
 * @return `true` when @p spec is non-null and its type accepts dotted
 * `name.key=value` entries; `false` otherwise.
 */
bool is_container(const ArgSpec* spec)
{
    return spec != nullptr
        && (spec->type == ArgType::string_map || spec->type == ArgType::object);
}

/**
 * @brief One argument accumulated across all its `--arg` pairs.
 *
 * A group collects every pair sharing one top-level name, preserving first-seen
 * order so the emitted object is deterministic. A group is either a container
 * (`is_map`, dotted entries accumulated in @ref entries) or a scalar/list (plain
 * values accumulated in @ref values); the two are mutually exclusive and a mix
 * on one name is a `duplicate_name`.
 */
struct arg_group
{
    ///< Top-level argument name (the part before the first `.` for a dotted
    ///< entry, or the whole name otherwise).
    std::string key;

    ///< The declaring spec, or `nullptr` for an unknown name.
    const ArgSpec* spec = nullptr;

    ///< `true` when this group accumulates dotted `string_map`/`object` entries.
    bool is_map = false;

    ///< Plain scalar/list values in source order (when `!is_map`).
    std::vector<std::string> values;

    ///< Dotted map/object entries, keyed by the part after the first `.`
    ///< (when `is_map`); sorted-key order fixes the emitted object text.
    std::map<std::string, std::string> entries;
};

/**
 * @brief Locates the group for @p key, creating it in first-seen order.
 *
 * @param groups The ordered group list, extended in place.
 * @param key Top-level argument name.
 * @param spec The spec declaring @p key (or `nullptr`).
 * @return A reference to the existing or newly appended group.
 */
arg_group& group_for(std::vector<arg_group>& groups,
                     std::string_view key,
                     const ArgSpec* spec)
{
    for (arg_group& group : groups) {
        if (group.key == key) {
            return group;
        }
    }
    arg_group fresh;
    fresh.key = std::string(key);
    fresh.spec = spec;
    groups.push_back(std::move(fresh));
    return groups.back();
}

}  // namespace

bool split_arg_pair(std::string_view text, arg_pair* out)
{
    const std::size_t eq = text.find('=');
    if (eq == std::string_view::npos || eq == 0) {
        return false;
    }
    if (out != nullptr) {
        out->name = std::string(text.substr(0, eq));
        out->value = std::string(text.substr(eq + 1));
    }
    return true;
}

bool coerce_args(const std::vector<ArgSpec>& specs,
                 const std::vector<arg_pair>& pairs,
                 std::string* args_json,
                 std::string* error)
{
    const auto fail = [error](const char* reason) {
        if (error != nullptr) {
            *error = reason;
        }
        return false;
    };

    // Pass 1: group pairs by top-level name, validating every name first so no
    // hostile token can reach the builder's programming-error throw (T-78).
    std::vector<arg_group> groups;
    for (const arg_pair& pair : pairs) {
        const std::size_t dot = pair.name.find('.');
        if (dot == std::string::npos) {
            if (!is_valid_name(pair.name)) {
                return fail("bad_name");
            }
            const ArgSpec* spec = find_spec(specs, pair.name);
            arg_group& group = group_for(groups, pair.name, spec);
            if (group.is_map) {
                return fail("duplicate_name");
            }
            const bool is_list = spec != nullptr
                && spec->type == ArgType::string_list;
            if (!group.values.empty() && !is_list) {
                return fail("duplicate_name");
            }
            group.values.push_back(pair.value);
            continue;
        }

        // Dotted name: the prefix must be a declared string_map/object arg, else
        // it cannot be expressed as a literal top-level key through the builder
        // and is rejected (bad_name). Both segments obey the name grammar.
        const std::string head = pair.name.substr(0, dot);
        const std::string tail = pair.name.substr(dot + 1);
        if (!is_valid_name(head)) {
            return fail("bad_name");
        }
        const ArgSpec* spec = find_spec(specs, head);
        if (!is_container(spec)) {
            return fail("bad_name");
        }
        if (tail.empty()) {
            return fail("map_entry_empty");
        }
        arg_group& group = group_for(groups, head, spec);
        if (!group.is_map) {
            if (!group.values.empty()) {
                return fail("duplicate_name");
            }
            group.is_map = true;
        }
        if (!group.entries.emplace(tail, pair.value).second) {
            return fail("map_entry_duplicate");
        }
    }

    // Pass 2: emit each group by its type. Every name is grammar-valid, so no
    // add_* call can throw.
    commands::args_builder builder;
    for (const arg_group& group : groups) {
        if (group.is_map) {
            builder.add_string_map(group.key, group.entries);
            continue;
        }
        const ArgSpec* spec = group.spec;
        if (spec != nullptr && spec->type == ArgType::string_list) {
            builder.add_string_list(group.key, group.values);
            continue;
        }

        const std::string& value = group.values.front();
        if (value == "null") {
            if (spec != nullptr && spec->nullable) {
                builder.add_null(group.key);
            } else {
                builder.add_string(group.key, value);
            }
            continue;
        }

        const ArgType type = spec != nullptr ? spec->type : ArgType::string;
        if (type == ArgType::integer) {
            if (const auto token = canonical_integer_token(value)) {
                builder.add_raw(group.key, *token);
            } else {
                builder.add_string(group.key, value);
            }
        } else if (type == ArgType::boolean) {
            if (value == "true") {
                builder.add_bool(group.key, true);
            } else if (value == "false") {
                builder.add_bool(group.key, false);
            } else {
                builder.add_string(group.key, value);
            }
        } else {
            builder.add_string(group.key, value);
        }
    }

    if (args_json != nullptr) {
        *args_json = builder.str();
    }
    return true;
}

}  // namespace url_shortener::cli
