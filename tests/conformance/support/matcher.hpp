#pragma once

/**
 * @file matcher.hpp
 * @brief C++ port of the shared conformance normalizer + matcher.
 *
 * A conformance case's `expect` block is compared against a live response
 * envelope by two independent implementations: the Python harness
 * (`tests/conformance/py/conformance_match.py`) and this C++ runner. The rules
 * are pinned as data in `tests/conformance/normalizer_vectors.json`; both
 * implementations must pass **every** vector in it unchanged, byte-for-byte. A
 * rule change edits the vectors first.
 *
 * This header is the C++ mirror of that contract. Its functions reproduce:
 * variable substitution (`${name}`), envelope normalization (rules N1/N4/N5),
 * structural matching with the matcher tokens (`<any>`/`<id>`/`<ts>`/`<int>`/
 * `<uint>`/`<slug:N>`/`<regex:...>`/`<var:name>`/`<absent>`), and the shared
 * regex-dialect / catastrophic-backtracking lint that refuses a dangerous
 * `<regex:...>` pattern **before** any `std::regex` is constructed.
 */

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mini_json.hpp"

namespace conformance
{

/**
 * @brief Variable bindings used by `${name}` substitution and `<var:name>`.
 *
 * Values are full JSON (not just strings) so the `<var:name>` token can compare
 * against a saved object or number (vectors `var_object_ok`, `var_number_ok`).
 * For `${name}` inlining a scalar value is stringified.
 */
using VariableMap = std::map<std::string, Json>;

/**
 * @brief Outcome of a structural match.
 */
struct MatchResult
{
    bool ok = false;  ///< `true` iff @ref findings is empty.

    ///< One `"path: reason"` string per mismatch; empty on a successful match.
    std::vector<std::string> findings;
};

/**
 * @brief The normalizer + matcher, as a set of stateless static operations.
 */
class Matcher
{
public:
    /**
     * @brief Expands every `${name}` reference inside @p value using @p vars.
     *
     * Objects and arrays are rebuilt recursively; non-string scalars pass
     * through unchanged. `${tmp}` has its backslashes rewritten to `/` so a
     * Windows temp path compares identically on every OS. The input is never
     * mutated.
     *
     * @param value Any JSON value.
     * @param vars The variable bindings.
     * @param error Optional out-parameter; set to a reason containing
     * `"unknown variable"` when a referenced name is not bound. When an error
     * occurs the returned value is unspecified and the caller must treat the
     * whole match as failed.
     * @return A new value with every `${name}` expanded (on success).
     */
    static Json substitute(const Json& value,
                           const VariableMap& vars,
                           std::string* error);

    /**
     * @brief Applies envelope normalization rules N1, N4 and N5 to @p envelope.
     *
     * - **N1**: `error.message` and `error.details` are dropped.
     * - **N4**: if the request carried an `id` and @p carries_id is `true`, the
     *   actual `id` must equal it; otherwise it must match `^srv-[0-9]+$`. A
     *   violation is appended to @p findings. Either way the value is replaced
     *   by the sentinel `"<id>"` so `expect` need not restate it.
     * - **N5**: any top-level key other than `v`, `id`, `ok`, `result`, `job`
     *   or `error` is appended to @p findings, regardless of match mode.
     *
     * N2/N3/N6 are realized by @ref match. The input is never mutated.
     *
     * @param envelope The actual response envelope (an object).
     * @param request The request envelope that produced it, or `nullptr` for a
     * raw-text case; only its `id` is read.
     * @param carries_id Whether the transport echoes the request `id`.
     * @param findings Out-parameter; receives one human-readable string per N4
     * or N5 violation (left untouched when the envelope is well-formed). A
     * non-empty result means the envelope fails to match in any mode.
     * @return The normalized envelope.
     */
    static Json normalize_actual(const Json& envelope,
                                 const Json* request,
                                 bool carries_id,
                                 std::vector<std::string>* findings);

    /**
     * @brief Structurally matches @p expected against @p actual.
     *
     * Realizes N2 (object key order ignored), N3 (numbers by value, a boolean is
     * never a number) and N6 (strings compared after NFC normalization, no
     * trimming), plus the matcher tokens.
     *
     * @param expected The `expect` value (may contain matcher tokens / `${var}`
     * already substituted).
     * @param actual The normalized actual value.
     * @param subset `false` for `exact` (an extra object key fails); `true` for
     * `subset` (extra object keys allowed; arrays still element-for-element).
     * @param vars Variable bindings consulted by the `<var:name>` token.
     * @return A @ref MatchResult; `ok` is `true` iff there are no findings.
     */
    static MatchResult match(const Json& expected,
                             const Json& actual,
                             bool subset,
                             const VariableMap& vars);

    /**
     * @brief Returns why @p pattern is outside the shared regex dialect.
     *
     * Rejects dialect mismatches (lookbehind, named groups / backreferences,
     * possessive quantifiers) and catastrophic-backtracking shapes (a quantified
     * group whose body itself contains a quantifier or an alternation:
     * `(a+)+`, `(a*)*`, `(.*a){20}`, `(a|a)+`). The check is purely syntactic and
     * never executes the pattern, so a ReDoS pattern cannot hang the matcher.
     *
     * @param pattern The regex text (the part after `<regex:`).
     * @return A short reason when @p pattern is disallowed, else `std::nullopt`.
     */
    static std::optional<std::string> forbidden_regex_reason(
        const std::string& pattern);
};

}  // namespace conformance
