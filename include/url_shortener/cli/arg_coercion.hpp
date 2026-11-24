/**
 * @file arg_coercion.hpp
 * @brief Turns the shell-friendly `cmd --arg <name>=<value>` form into the typed
 * `args` JSON object a command expects, using the command's own @ref
 * url_shortener::commands::ArgSpec list as the only schema.
 *
 * This is the "flags compiled from `ArgSpec`" half of the generic `cmd`
 * subcommand (ADR 0002): one definition - the descriptor - decides how
 * `limit=10` becomes the integer `10` and `enabled=true` becomes a boolean. It
 * is a pure library with no transport dependency; the parsing of the CLI tokens
 * (subtask 12) and the execution (subtask 13) live elsewhere. The header is free
 * of Boost.ProgramOptions and Beast includes (milestone contract C9).
 *
 * The coercer never validates domain rules (URLs, slugs, enum membership): it
 * only chooses the JSON *type* for each value from the arg's @ref
 * url_shortener::commands::ArgType. Every rejection a user ultimately sees comes
 * from the single `validate_args` path in the registry, not from here - an
 * unknown name, a wrong-typed integer or a bad boolean is emitted as a JSON
 * string so the registry reports it (`<arg>: unknown` / `<arg>: wrong_type`).
 * The `args` text is assembled only through `commands::args_builder`, so JSON
 * escaping lives in exactly one place and no argv byte is ever concatenated into
 * the output (threat model T-78): a hostile name can neither forge a member nor
 * reach the builder's programming-error throw.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include <url_shortener/commands/arg_spec.hpp>

namespace url_shortener::cli
{

/**
 * @brief One `--arg` operand as the two halves of `<name>=<value>`.
 *
 * Produced by @ref split_arg_pair (or by the subtask-12 parser) from a single
 * `--arg` token. Both halves are stored verbatim; neither is validated or
 * escaped here. @ref name is the undotted-or-dotted argument name exactly as the
 * user typed it; @ref value is everything after the first `=`, so it may itself
 * contain `=`, quotes, backslashes or newlines.
 */
struct arg_pair
{
    ///< Argument name as typed after `--arg`, before the first `=`. May be a
    ///< plain name (`limit`) or a dotted map/object path (`metadata.utm`).
    std::string name;

    ///< Argument value as typed after the first `=`; arbitrary untrusted text,
    ///< never escaped or inspected by @ref split_arg_pair.
    std::string value;
};

/**
 * @brief Splits a `<name>=<value>` token at its first `=`.
 *
 * The split is on the *first* `=` only, so a value may contain further `=`
 * characters (`a=b=c` -> name `a`, value `b=c`). No grammar check is applied to
 * the name here; that is @ref coerce_args's job. The caller maps a `false`
 * return on a token lacking `=` to the `bad_pair` error and a `false` return on
 * an empty name to `bad_name` (the two cases it distinguishes by inspecting the
 * token itself).
 *
 * @param text The raw `--arg` operand token.
 * @param out When non-null and the token splits, populated with the name and
 * value; left unchanged when the function returns `false`.
 * @return `true` when @p text contains a `=` preceded by a non-empty name;
 * `false` when @p text has no `=` or the name before the first `=` is empty.
 */
bool split_arg_pair(std::string_view text, arg_pair* out);

/**
 * @brief Builds the `args` object text for one command from its `--arg` pairs.
 *
 * Groups @p pairs by argument name (preserving first-seen order), then emits
 * each group as the JSON type its matching @ref url_shortener::commands::ArgSpec
 * declares, via `commands::args_builder`:
 *
 * - `string`, `enumeration`, `timestamp`, and any **unknown** name: the value
 *   as a JSON string (so the registry validates it, or reports it unknown).
 * - `integer`: the value as a JSON number when it matches `^[0-9]{1,18}$`
 *   (leading zeros stripped, base 10, never octal); otherwise as a JSON string
 *   so the registry reports `<arg>: wrong_type`. A leading sign is *not*
 *   accepted, matching the REST query coercion (`canonical_integer_token`).
 * - `boolean`: `true`/`false` as a JSON boolean; any other token as a JSON
 *   string (registry rejects it).
 * - `string_list`: built by repeating the name (`tags=a tags=b` ->
 *   `["a","b"]`); a single `tags=x` is `["x"]` and `tags=` is `[""]`. List
 *   elements are kept literal (commas are not split). Empty lists use `--json`.
 * - `string_map` / `object`: built from dotted names (`metadata.utm=email` ->
 *   `{"metadata":{"utm":"email"}}`). Map keys (the part after the first `.`) are
 *   data, escaped by the builder.
 * - `nullable` arg with the exact value `null`: JSON `null` (clears a field); a
 *   non-nullable arg with `null` is an ordinary string.
 *
 * The coercer is total on hostile input (never throws): every name is validated
 * against `[a-z][a-z0-9_]*` before it reaches the builder, so the builder's
 * programming-error throw is unreachable (threat model T-78).
 *
 * @param specs The argument schema of the target command; may be empty (unknown
 * command), in which case every pair is emitted as a JSON string.
 * @param pairs The `--arg` name/value pairs in command-line order.
 * @param args_json When non-null and the call succeeds, set to the assembled
 * single JSON object text (`"{}"` when @p pairs is empty); left unchanged on
 * failure.
 * @param error When non-null and the call fails, set to exactly one fixed,
 * value-free string (the supplied values are never echoed, as they may be
 * secrets in shell history): `bad_name` (an empty name, a name outside
 * `[a-z][a-z0-9_]*`, or a dotted name whose prefix is not a `string_map`/
 * `object` arg), `duplicate_name` (a non-`string_list` name repeated, or a
 * dotted and a plain use of the same name), `map_entry_empty` (a dotted name
 * with an empty key after the `.`), or `map_entry_duplicate` (the same dotted
 * key given twice); left unchanged on success.
 * @return `true` when every pair was coerced and @p args_json was written;
 * `false` on the first violation, with @p error set.
 */
bool coerce_args(const std::vector<commands::ArgSpec>& specs,
                 const std::vector<arg_pair>& pairs,
                 std::string* args_json,
                 std::string* error);

}  // namespace url_shortener::cli
