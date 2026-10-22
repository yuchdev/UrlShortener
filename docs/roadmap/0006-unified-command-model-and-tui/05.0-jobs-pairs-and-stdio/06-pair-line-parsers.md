# 06 - Pair line parsers (CSV, NDJSON) and capped error list

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** none (pure functions)
**Blocks:** 07, 08
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Provide the pure, line-oriented parsers used by `link.import`: CSV (`full_url,slug`) and NDJSON (one `link.create` args object per line), plus the `ImportErrorList` that caps reported per-line errors at 100. The parsers never touch storage or files and never validate URLs/slugs (that is `LinkCommandService::CreateLink`'s job, so there is no second validation path).

## Dependencies

- None at build time besides the standard library. `core/utils.h` helpers (`trim`) may be used in the `.cpp` only.
- Format contract: catalog section 3.3 (`csv`: `full_url,slug` per line, slug may be blank; `ndjson`: one `link.create` args object per line).

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/pair_line_parser.hpp` | create | row types, `PairLineParser`, `ImportErrorList`, limits |
| `src/commands/pair_line_parser.cpp` | create | implementation |
| `tests/unit/commands/55_pair_line_parser.cpp` | create | cases below |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/commands/pair_line_parser.cpp`; test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

`namespace url_shortener::commands` (all NEW).

```cpp
enum class PairFormat { csv, ndjson };
inline constexpr std::size_t kMaxImportLineBytes = 16384;   // enforced by the line reader (07)
inline constexpr std::size_t kMaxImportRows = 100000;       // enforced by 08
inline constexpr std::size_t kMaxReportedImportErrors = 100;

enum class RowKind { pair, skipped, error };                // skipped = blank line or header
struct ParsedRow {
    RowKind kind = RowKind::skipped;
    std::size_t line = 0;                                   // 1-based physical line
    std::string full_url;                                   // csv
    std::optional<std::string> slug;                        // csv; blank -> nullopt (generate)
    std::string args_json;                                  // ndjson: the object text, unvalidated
    std::string error_code;                                 // "invalid_field" when kind == error
};

class PairLineParser {
public:
    explicit PairLineParser(PairFormat format);
    ParsedRow parse(std::string_view line, std::size_t line_number);   // stateful: header detection
private:
    PairFormat m_format; bool m_seen_first_data_line = false;
};

class ImportErrorList {
public:
    void add(std::size_t line, std::string code);           // keeps first kMaxReportedImportErrors
    const std::vector<std::pair<std::size_t, std::string>>& items() const noexcept;
    std::size_t omitted() const noexcept;                   // errors beyond the cap
    std::string to_json() const;                            // [{"line":n,"code":"..."},...]
};
```

CSV rules (RFC 4180 subset, line based):

- Strip a UTF-8 BOM on line 1 and a trailing `\r`.
- Blank / whitespace-only line: `skipped`.
- First non-blank line equal (ASCII case-insensitive, after trim) to `full_url,slug` or `full_url`: header, `skipped`.
- Fields split on `,`; a field may be wrapped in `"` with `""` as an escaped quote; a comma inside quotes is data; an unterminated quote, a quote in the middle of an unquoted field, or more than 2 fields -> `error` / `invalid_field`. Embedded newlines inside quoted fields are NOT supported (line based) and surface as an unterminated quote error.
- Field 1 required non-empty after trim, else `error`. Field 2 blank or absent -> `slug = nullopt`.
- Fields are trimmed of surrounding spaces only outside quotes.

NDJSON rules: trim; blank -> `skipped`; must start with `{` and end with `}` (single object, no trailing garbage) else `error`; the text is returned in `args_json` for the shared `link.create` args decoding (the `link.create` `ArgSpec` validation plus the codec of 03.0/05, used by the 03.0/07 handler) which owns key and type validation (unknown key -> `invalid_field`). A `#`-prefixed comment line is NOT special in either format.

## Implementation steps

1. Header with types and limits; no Beast include.
2. Implement CSV tokenizer as a small state machine (unquoted, quoted, after-quote).
3. Implement NDJSON structural check by scanning for balanced braces while honoring string literals and `\` escapes, so `{"a":"}"}` is one object and `{"a":1}{"b":2}` is an error.
4. Implement `ImportErrorList` with `std::vector` and counters; `to_json` uses `jsonEscape` for codes (in `.cpp`).
5. Tests, including a 100/101/1000 error boundary.

## Commands

```text
cmake --build cmake-build --target commands__55_pair_line_parser
ctest --test-dir cmake-build -R "^commands__55_pair_line_parser$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/55_pair_line_parser.cpp` | `commands__55_pair_line_parser` | unit | CSV: plain row; blank slug (`https://a.example/x,`); one-column row; header skipped (any case) only when first; header-looking line later is a data error; quoted field with comma; `""` escape; unterminated quote; 3 columns; empty URL; CRLF and BOM; whitespace-only line; very long (kMaxImportLineBytes) accepted by parser (limit is the reader's); NDJSON: valid object; nested braces and `}` inside strings; two objects on one line; trailing garbage; array line; blank; truncated object; `ImportErrorList`: 0, 100, 101, 1000 adds -> sizes 0/100/100/100 and `omitted` 0/0/1/900; `to_json` valid and escaped |

Manual check: none.

## Constraints

- Plan C1: no validation of URL or slug here; `CreateLink` is the single path (SSRF `isPrivateHost`, slug rules, reserved slugs).
- C9: no JSON library; the NDJSON check is structural only.
- C6: error entries carry line number and code only, never the offending content.
- C++17; `lower_case`; `.cpp` in `sources.cmake`.

## Success criteria

1. `commands__55_pair_line_parser` passes.
2. A fuzz-style table test of 30 malformed lines never throws and never returns `RowKind::pair` for them.
3. `grep -n "isValidSlug\|normalizeTargetUrl\|isPrivateHost" src/commands/pair_line_parser.cpp` returns nothing.

## Out of scope / risks

- Multi-line quoted CSV fields, other delimiters, encodings other than UTF-8: unsupported in V1.
- Risk: NDJSON key validation depends on the 03.0/05 codec exposing a reusable "args JSON -> `CreateLinkCommand`" function; if it does not, 08 must call the registry-level `ArgSpec` validation for `link.create` instead (still one path).
