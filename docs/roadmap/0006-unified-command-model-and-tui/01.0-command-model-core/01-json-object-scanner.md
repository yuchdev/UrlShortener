# 01 - JSON object scanner

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 03, 05, 04.0/01, 05.0/14, 06.0/02
**Estimated size:** M
**Suggested agent:** cpp-expert (tests: testing-expert; review: security-auditor, because this parses untrusted input)

## Objective

Add a small, bounded, dependency-free scanner that splits one JSON *object*
into its top-level members (key, kind, raw token). It is the only code in the
commands layer that walks JSON text; the envelope parser (03) and `ArgsView`
(05) are built on it. It replaces the first-occurrence key search of
`core/utils.h` for envelopes, because that search matches nested keys and
string contents, cannot read numbers/booleans and cannot enumerate keys
(needed for "unknown keys are rejected").

## Dependencies

- None in the commands layer.
- Reuses nothing at runtime from `core/utils.h` except (optionally) leaf
  string un-escaping if a verified helper exists; otherwise this subtask owns
  a small decoder (see "Classes and functions").
- No new library (CLAUDE.md, milestone C9).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/json_scan.hpp` | create | Scanner types and functions |
| `src/commands/json_scan.cpp` | create | Iterative scanner and string decoder |
| `tests/unit/commands/01_json_scan.cpp` | create | Table-driven tests |
| `sources.cmake` | modify | Add `src/commands/json_scan.cpp` to `COMMON_CPP_SOURCES`; create list `COMMANDS_UNIT_SOURCES` with the test |
| `CMakeLists.txt` | modify | Add `register_labeled_cpp_tests(COMMANDS_UNIT_SOURCES "unit")` next to the other `register_labeled_cpp_tests` lines (around line 173-192) |

Reminder (CLAUDE.md): a `.cpp` that is not in `sources.cmake` is silently not
built; the legacy `URL_SHORTENER_CPP` list is not used by any target, only
`COMMON_CPP_SOURCES` feeds `url_shortener_common`.

## Classes and functions

All in `namespace url_shortener::commands`. No exceptions for malformed input;
errors are returned.

```cpp
enum class json_kind { string, number, boolean, null, object, array };

struct json_member
{
    std::string key;     // decoded key
    json_kind kind;      // kind of the value
    std::string raw;     // exact source text of the value; for strings the
                         // text including the surrounding quotes
};

struct scan_limits
{
    std::size_t max_bytes   = 65536;  // default mirrors ServerConfig::max_request_body_bytes
    int         max_depth   = 8;      // nesting of objects/arrays inside values
    std::size_t max_members = 256;    // top-level members
    std::size_t max_key_len = 128;
};

struct scan_result
{
    bool ok = false;
    std::vector<json_member> members;
    std::string error;   // fixed vocabulary: "not_an_object", "too_large",
                         // "too_deep", "too_many_members", "duplicate_key",
                         // "malformed", "trailing_data"
};

/// Scan @p text, which must be exactly one JSON object (leading/trailing
/// whitespace allowed). Iterative (no recursion), O(n), never throws.
scan_result scan_object(std::string_view text, const scan_limits& limits = {});

/// Decode a JSON string token (including quotes) to UTF-8. Handles \" \\ \/
/// \b \f \n \r \t and \uXXXX including surrogate pairs. Returns nullopt on a
/// bad escape or an unpaired surrogate.
std::optional<std::string> decode_json_string(std::string_view raw_token);

/// Lookup helper: first member with this key, or nullptr.
const json_member* find_member(const std::vector<json_member>& members,
                               std::string_view key);

struct json_element { json_kind kind; std::string raw; };
struct array_scan_result
{
    bool ok = false;
    std::vector<json_element> elements;
    std::string error;   // same vocabulary; "not_an_array" instead of "not_an_object"
};

/// Scan @p text, which must be exactly one JSON array, into its top-level
/// elements (same limits, `max_members` caps the element count). Used for
/// `string_list` args and by the TUI to split `result.items` / `rows`.
array_scan_result scan_array(std::string_view text, const scan_limits& limits = {});
```

This header is the **single sanctioned JSON tokenizer** of the product code
(ADR 0002, plan C9, decision log D3). It includes no Beast, storage or http
header, so `src/tui/` (Task 06.0 subtask 02), the args builder (Task 04.0
subtask 01) and the pair/stdio code (Task 05.0) include it directly instead of
writing their own scanners.

UNVERIFIED: whether `core/utils.h::extractJsonStringField` already decodes
`\uXXXX`; the spec assumes it does not and owns the decoder. Do not call
`extractJsonStringField` for envelope fields.

Nested values (objects/arrays) are returned as raw text only; callers re-scan
a nested object by calling `scan_object(member.raw)` (used for `args`,
`options`, and object-typed args).

## Implementation steps

1. Create `json_scan.hpp` with the declarations above and Doxygen comments.
2. Implement a single-pass state machine over `text`: skip whitespace, expect
   `{`, then loop `key : value [, ...]` until `}`.
3. For values, record the start offset, advance with a depth counter that is
   string- and escape-aware (the same technique as the loop in
   `extractJsonValueToken`, but with an explicit `max_depth` and no
   recursion), and capture `raw`.
4. Classify `kind` by the first character: `"` string, `{` object, `[` array,
   `t`/`f` boolean (must match `true`/`false` exactly), `n` null (`null`),
   otherwise number; validate numbers with the JSON number grammar
   (no leading `+`, no leading zeros, optional fraction/exponent).
5. Enforce limits before and during the scan (`max_bytes` on entry;
   `max_depth`, `max_members`, `max_key_len` as encountered).
6. Reject duplicate top-level keys (`duplicate_key`) and any non-whitespace
   after the closing `}` (`trailing_data`).
7. Implement `decode_json_string` and `find_member`.
8. Write the tests (below), add the sources, register, build, run.

## Commands

```powershell
# configure once (Windows: vcpkg toolchain required, see README)
cmake -S . -B cmake-build -G Ninja
cmake --build cmake-build --target commands__01_json_scan
ctest --test-dir cmake-build -R "^commands__01_json_scan$" --output-on-failure
```

Multi-config generators (Visual Studio): add `--config Debug` to the build and
`-C Debug` to `ctest`. The test target name is `<parent_dir>__<stem>` per
CLAUDE.md, hence `commands__01_json_scan`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/01_json_scan.cpp` | `commands__01_json_scan` | unit | see below |

Cases:

- Happy path: members of `{"v":1,"id":"c-1","ok":true,"x":null,"a":{"k":[1,2]}}`
  with correct `kind`/`raw` for each.
- Nested look-alike keys: `{"args":{"command":"evil"},"command":"link.get"}`
  yields exactly two top-level members; `find_member("command")->raw` is
  `"link.get"` (the regression the utils helpers fail).
- Key text inside a string value is not a key: `{"a":"\"b\":1","b":2}`.
- Numbers: `0`, `-1`, `1.5`, `1e3` ok; `01`, `+1`, `1.`, `.5` -> `malformed`.
- Booleans/null exact spelling; `True`, `nul` -> `malformed`.
- Escapes: `é`, surrogate pair `😀` decode to UTF-8; lone
  surrogate and `\x` -> decode returns `nullopt`.
- Limits: 65537-byte body -> `too_large`; depth 9 -> `too_deep`; 257 members ->
  `too_many_members`; 129-char key -> `malformed`; default limits overridden by
  a custom `scan_limits`.
- `scan_array`: `[]`, `["a",1,{"k":[2]},null]` (kinds and raw), nested arrays
  as raw text, trailing comma -> `malformed`, object input -> `not_an_array`,
  `max_members + 1` elements -> `too_many_members`.
- Structure: top-level array / scalar / empty string -> `not_an_object`;
  `{}` ok with zero members; trailing `}` or `,` -> `trailing_data`/`malformed`;
  duplicate key -> `duplicate_key`.
- Robustness: 1 MB of `[` characters returns `too_large` quickly (no stack
  growth); truncated input at every prefix length of a sample document never
  crashes and never returns `ok`.

Manual check: none (pure library).

## Constraints

- No JSON library; no recursion (stack exhaustion on hostile input);
  untrusted input handling per CLAUDE.md "Treat all ... request bodies as
  untrusted".
- C++17 only (`std::string_view`, `std::optional`); no `<regex>` in the
  scanner (performance and ReDoS).
- Naming: lower_case types/functions as above; no globals.
- Milestone contracts: C2 (envelope is the only wire shape - this is its
  tokenizer), C9 (no JSON library).
- Never log the scanned text or `raw` values (they may hold secrets).

## Success criteria

1. `commands__01_json_scan` builds and passes via the command above.
2. The nested-look-alike case proves a top-level key is never taken from a
   nested object (test asserts it).
3. All limit cases return the documented `error` string and never throw
   (test asserts `noexcept` behavior by running under the test with
   `BOOST_CHECK_NO_THROW`).
4. `src/commands/json_scan.cpp` contains no `#include <regex>` and no
   recursive call to `scan_object` (checked by review / grep).

## Out of scope / risks

- Not a general JSON parser: no DOM, no number-to-double conversion, no
  pretty-printing. Number leaf conversion is done by callers (05); display
  formatting (the TUI's pretty printer) is built by its callers on top of
  `scan_object`/`scan_array`.
- Strict RFC 8259 conformance of every corner (e.g. invalid UTF-8 in raw
  strings is passed through) is not promised; UTF-8 validation is a possible
  hardening follow-up.
- Duplicate-key rejection is stricter than many parsers; it is deliberate so
  the same envelope cannot mean two things on two transports.
