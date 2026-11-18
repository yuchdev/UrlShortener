#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace url_shortener::tui
{

/**
 * @brief JSON *view* helpers for reading and re-indenting command `result`
 * payloads (ADR 0003, contract C9).
 *
 * Every function here is a thin wrapper over the one sanctioned scanner
 * (`commands/json_scan.hpp`, Task 01.0 subtask 01): none tokenizes JSON
 * itself. They are read-only accessors - they never validate a schema, mutate
 * input, or sanitize for a terminal (T-110 sanitization is the render sinks'
 * job). All of them accept arbitrary, possibly-malformed input and report a
 * miss as `std::nullopt` / empty / verbatim rather than throwing. Inputs are
 * scanned under an 8 MiB byte cap so a large `link.list` page is accepted while
 * hostile input stays bounded (threat model T-121).
 */

/**
 * @brief Returns the raw JSON source text of one top-level field.
 *
 * @param json A JSON object text to read.
 * @param field The field name to look up.
 * @return The field value's exact source token (quotes for a string,
 * brace/bracket to brace/bracket for a container); `std::nullopt` when @p json
 * is not a scannable object within limits or has no such field.
 */
std::optional<std::string> raw_field(const std::string& json,
                                     const std::string& field);

/**
 * @brief Splits a JSON array into the raw source text of its elements.
 *
 * Delegates to `commands::scan_array`, so quotes, braces and brackets inside
 * string elements never mis-split the array.
 *
 * @param raw_array A JSON array text (e.g. a `result.items` value).
 * @return The elements' raw source tokens in order; an empty vector when
 * @p raw_array is not a scannable array within limits (including `[]`).
 */
std::vector<std::string> split_array(const std::string& raw_array);

/**
 * @brief Reads a top-level field as a decoded string.
 *
 * @param json A JSON object text to read.
 * @param field The field name to look up.
 * @return The decoded UTF-8 string when the field is present with a string
 * value; `std::nullopt` when @p json is not a scannable object, the field is
 * absent, its value is not a string, or the token fails to decode.
 */
std::optional<std::string> get_string(const std::string& json,
                                      const std::string& field);

/**
 * @brief Reads a top-level field as a signed 64-bit integer.
 *
 * @param json A JSON object text to read.
 * @param field The field name to look up.
 * @return The parsed value when the field is present as a JSON integer token
 * fitting `std::int64_t`; `std::nullopt` when @p json is not a scannable
 * object, the field is absent, its value is not a number, is fractional or has
 * an exponent, or is out of range.
 */
std::optional<std::int64_t> get_int(const std::string& json,
                                    const std::string& field);

/**
 * @brief Reads a top-level field as a boolean.
 *
 * @param json A JSON object text to read.
 * @param field The field name to look up.
 * @return `true`/`false` when the field is present as a JSON boolean;
 * `std::nullopt` when @p json is not a scannable object, the field is absent,
 * or its value is not a boolean.
 */
std::optional<bool> get_bool(const std::string& json, const std::string& field);

/**
 * @brief Re-indents a JSON value for display without reordering or altering it.
 *
 * Walks the value with `commands::scan_object`/`scan_array` recursively and
 * re-emits it with @p indent spaces per level. Member order is preserved and
 * string/number tokens are emitted verbatim, so the only change is whitespace
 * (the output is golden-testable). Recursion is bounded by the scanner depth
 * cap and the function never throws: a value that is not well-formed within
 * limits (including trailing data) is returned verbatim.
 *
 * @param json The JSON value text to pretty-print.
 * @param indent Spaces of indentation per nesting level; negatives are treated
 * as `0` and the width is bounded for defense in depth. Defaults to `2`.
 * @return The re-indented text, or @p json unchanged when it is not a single
 * well-formed JSON value within limits.
 */
std::string pretty_print(const std::string& json, int indent = 2);

}  // namespace url_shortener::tui
