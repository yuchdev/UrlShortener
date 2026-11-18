#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace url_shortener::tui
{

/**
 * @file field_parsers.hpp
 * @brief FTXUI-free parsers and their inverse formatters for the comma/equals
 * editing forms the @ref FormModel uses for list, map and integer fields
 * (Task 06.0 subtask 07).
 *
 * These helpers belong to `url_shortener_tui_model`: they are pure, do no I/O,
 * and never tokenize JSON (contract C9) - a list is a comma-separated line and
 * a map is a `k=v` line, both edited as plain text. The `parse_*` functions
 * turn a user-typed line into values; the `format_*` functions turn values back
 * into a line for the edit-prefill flow and report whether the values are
 * *faithfully* representable in the comma/equals form (a value containing `,`
 * or `=` is not, and the caller surfaces the "use the JSON form" escape hatch
 * rather than silently corrupting data). None of these functions sanitizes for
 * a terminal; the text they produce is echoed by the view, which applies
 * `sanitize_for_terminal` at the render sink (threat model T-110).
 */

/**
 * @brief Splits a comma-separated line into trimmed, non-empty items.
 *
 * Each comma-delimited field has its leading and trailing ASCII spaces and tabs
 * removed; an item that is empty after trimming is dropped. So `"a, b ,c"`
 * yields `{"a","b","c"}`, `"a,,b,"` yields `{"a","b"}`, and an empty or
 * whitespace-only line yields an empty vector. Commas cannot be escaped: a
 * value that must contain a comma is not representable in this form (see
 * @ref format_string_list).
 *
 * @param s The raw, user-typed line; interpreted as UTF-8 but split on ASCII
 * commas only.
 * @return The parsed items in source order with empties dropped; never throws.
 */
std::vector<std::string> parse_string_list(const std::string& s);

/**
 * @brief Parses a `k=v, k2=v2` line into ordered key/value pairs.
 *
 * The line is split on commas; each non-empty entry is split on its *first* `=`
 * into a key and a value, both ASCII-trimmed. A later entry with a key already
 * seen replaces that key's value in place (last value wins, first position
 * kept). An entry with no `=` or an empty key makes the whole parse fail. A
 * value may be empty (`"k="` yields key `k` -> value `""`). An empty or
 * whitespace-only line yields an empty vector (an explicit empty map).
 *
 * @param s The raw, user-typed line; interpreted as UTF-8 but split on ASCII
 * `,` and `=` only.
 * @param duplicate_keys When non-null, each key that appeared more than once
 * (and whose earlier value was overwritten by a later one) is appended here, in
 * first-seen order, so a caller can surface a non-blocking "last wins" warning;
 * left untouched when there are no duplicates. Optional; defaults to `nullptr`.
 * @return The ordered, de-duplicated pairs; `std::nullopt` when any entry is
 * missing its `=` or has an empty key. Never throws.
 */
std::optional<std::vector<std::pair<std::string, std::string>>>
parse_string_map(const std::string& s,
                 std::vector<std::string>* duplicate_keys = nullptr);

/**
 * @brief Parses a user-typed line as a signed 64-bit integer.
 *
 * The line is ASCII-trimmed, then read as an optional `+`/`-` sign followed by
 * one or more decimal digits with no other characters. Overflow of the
 * `long long` range is rejected rather than wrapped, and `INT64_MIN` is
 * accepted exactly. Leading zeros are accepted and normalized away by the
 * caller when it re-emits the value with `std::to_string`.
 *
 * @param s The raw, user-typed line.
 * @return The parsed value; `std::nullopt` for an empty line, a non-digit
 * character, a bare sign, or a value outside the `long long` range.
 */
std::optional<long long> parse_integer(const std::string& s);

/**
 * @brief Joins list items into a `", "`-separated line for edit prefill.
 *
 * The inverse of @ref parse_string_list for faithfully representable input:
 * `parse_string_list(format_string_list(x, r)) == x` whenever every item is
 * non-empty, carries no surrounding whitespace and contains no comma (in which
 * case @p representable is set `true`). An item containing a comma is still
 * written verbatim, but @p representable is set `false` so the caller can warn
 * and point the operator at the JSON form rather than emit a line that would
 * parse back differently.
 *
 * @param items The list values to render.
 * @param representable Set to `true` when the produced line round-trips through
 * @ref parse_string_list, `false` when any item contains a comma.
 * @return The `", "`-joined line.
 */
std::string format_string_list(const std::vector<std::string>& items,
                               bool& representable);

/**
 * @brief Joins key/value pairs into a `"k=v, k2=v2"` line for edit prefill.
 *
 * The inverse of @ref parse_string_map for faithfully representable input:
 * `parse_string_map(format_string_map(x, r)) == x` whenever every key is
 * non-empty and no key or value carries surrounding whitespace or contains a
 * `,` or `=` (in which case @p representable is set `true`). A pair whose key
 * or value contains `,` or `=` is still written verbatim, but @p representable
 * is set `false` so the caller can surface the JSON escape hatch.
 *
 * @param pairs The key/value pairs to render.
 * @param representable Set to `true` when the produced line round-trips through
 * @ref parse_string_map, `false` when any key or value contains `,` or `=`.
 * @return The `", "`-joined `k=v` line.
 */
std::string format_string_map(
    const std::vector<std::pair<std::string, std::string>>& pairs,
    bool& representable);

}  // namespace url_shortener::tui
