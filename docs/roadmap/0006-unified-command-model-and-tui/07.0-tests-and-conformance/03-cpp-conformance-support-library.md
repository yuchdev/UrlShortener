# 03 - C++ conformance support library

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** 01, 02
**Blocks:** 04, 10
**Estimated size:** L
**Suggested agent:** testing-expert (implementation via cpp-expert if production headers are needed - they must not be)

## Objective

Provide the test-only C++ building blocks for in-process transports: a minimal
JSON value/parser (tests may not add a JSON library), corpus loader, matcher
and normalizer ported from subtask 02, the adapter interface, and the case
runner. Passes the shared `normalizer_vectors.json`.

## Dependencies

- Subtasks 01 (schema) and 02 (rules + vectors).
- `CMakeLists.txt` precedent: `APP_UNIT_SOURCES` loop (dedicated include dir).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/conformance/support/mini_json.hpp` / `.cpp` | create | `conformance::Json` value + parser + serializer (RFC 8259 subset: no comments, UTF-8, depth <= 64). **Test-only** (decision log D3): it lives under `tests/`, is linked only into `conformance_support`, and is deliberately independent of the product scanner (`commands::scan_object`, 01.0/01) so a scanner bug cannot hide itself in the conformance verdict. Product code must never include it (checked by 07.0/21's structural test). |
| `tests/conformance/support/corpus.hpp` / `.cpp` | create | `Case`, `Step`, `Expect` structs; `CorpusLoader` |
| `tests/conformance/support/matcher.hpp` / `.cpp` | create | `Matcher`, `normalizeActual`, `substitute` |
| `tests/conformance/support/adapter.hpp` | create | `Transport`, `TransportCaps`, `IConformanceAdapter` |
| `tests/conformance/support/case_runner.hpp` / `.cpp` | create | `CaseRunner`, `RunReport` |
| `tests/conformance/runners/00_support_selftest.cpp` | create | Boost.Test: mini_json round trips + `normalizer_vectors.json` |
| `sources.cmake` | modify | New `CONFORMANCE_SUPPORT_CPP` list (the `.cpp` under `support/`) and `CONFORMANCE_CPP_SOURCES` (runners) |
| `CMakeLists.txt` | modify | New static lib `conformance_support` (links `url_shortener_common`), loop registering `conformance__<stem>` per README conventions; `target_compile_definitions(... CONFORMANCE_CORPUS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/conformance")` |

Support `.cpp` files are not part of `url_shortener_common` (they must never ship in the product library); they are listed in their own variable so the CLAUDE.md "list every `.cpp`" rule is satisfied.

## Classes and functions

```cpp
namespace conformance {

class Json {                                   // tagged value; objects keep insertion order
public:
    enum class Kind { Null, Bool, Int, Double, String, Array, Object };
    static std::optional<Json> parse(std::string_view text, std::string* error);
    std::string dump() const;                  // compact, stable key order = insertion
    Kind kind() const;
    const Json* find(std::string_view key) const;          // object lookup or nullptr
    const Json* at(std::size_t i) const;
    const Json* path(std::string_view jsonpath) const;     // "$.a.b[0]" subset
    // typed getters: asString(), asInt(), asBool(); programmer error asserts
};

enum class Transport { Direct, RestGeneric, RestResource, CliCmd, Stdio, Tui };
std::string_view transportId(Transport);       // "direct", "rest_generic", ...

struct TransportCaps { bool carries_id, carries_raw, carries_options; };

struct Step { std::string label, caller; Json request; std::string raw_request;
              Json expect, expect_accept; Json save; /* var->path */
              std::optional<AwaitJob> await_job; std::optional<RestResource> rest_resource;
              std::optional<int> http_status, cli_exit; };
struct Case { std::string name, doc, command, caller, backend; std::vector<Step> steps;
              std::map<std::string,std::string> files;
              std::set<Transport> only; std::map<Transport,std::string> skip; };

class CorpusLoader {
public:
    static std::vector<Case> loadDirectory(const std::filesystem::path& dir, std::string* error);
};

struct MatchResult { bool ok; std::vector<std::string> findings; };
class Matcher {
public:
    static Json substitute(const Json&, const std::map<std::string,std::string>& vars);
    static Json normalizeActual(const Json& envelope, const Json* request,
                                bool carries_id, std::vector<std::string>* findings);
    static MatchResult match(const Json& expected, const Json& actual,
                             bool subset, const std::map<std::string,std::string>& vars);
};

class IConformanceAdapter {
public:
    virtual ~IConformanceAdapter() = default;
    virtual Transport transport() const = 0;
    virtual TransportCaps caps() const = 0;
    // Returns empty optional if the case can run, else the skip reason.
    virtual std::optional<std::string> beginCase(const Case&, const std::filesystem::path& tmp) = 0;
    // Sends one step's request; returns the *actual* response envelope text.
    virtual std::string send(const Step&, const std::string& request_text) = 0;
    virtual void endCase() = 0;
};

struct RunReport { int ran = 0, skipped = 0, failed = 0; std::vector<std::string> failures; };
class CaseRunner {
public:
    RunReport run(const std::vector<Case>&, IConformanceAdapter&);
};
}
```

`CaseRunner::run` per case: resolve skip (`only`/`skip`/capability: `raw_request` needs `carries_raw`), create temp dir, write `files`, `beginCase`, loop steps (substitute, `send`, parse, `normalizeActual`, `match`, `save` vars, `await_job` polling through `send` of a `job.get` envelope with 25 ms sleeps), `endCase`, delete temp dir. Failure text: `transport/case/step[label]: <json path>: <reason>` plus both JSON documents.

## Implementation steps

1. `mini_json`: parse/dump; reject duplicate keys, trailing commas, depth > 64; Int vs Double split (no float for ints).
2. Port `Matcher` rule by rule from `conformance_match.py`; `std::regex` ECMAScript for `<regex:>`.
3. `CorpusLoader`: read every `*.json` sorted by name; apply the same structural checks as the lint (names unique); semantic validation stays in the linter.
4. `CaseRunner` with injectable sleep (for the self-test).
5. `CaseRunner` writes a summary JSON `{transport, ran, skipped:[{case,reason}]}` to `$URLSHORTENER_CONFORMANCE_OUT` (default: temp dir) for the coverage gate (subtask 15).
6. `00_support_selftest.cpp`: load `normalizer_vectors.json` and run every vector; add `mini_json` fuzz-lite (20 malformed inputs must error, not crash).
7. CMake wiring; confirm `conformance__00_support_selftest` builds on MSVC with `/bigobj` already set globally.

## Commands

```bash
cmake --build cmake-build --target conformance__00_support_selftest
ctest --test-dir cmake-build -R "^conformance__00_support_selftest$" --output-on-failure
# Windows / multi-config: add -C Debug to both
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/conformance/runners/00_support_selftest.cpp` | `conformance__00_support_selftest` | `integration;conformance` | all vectors; json round trip; malformed json; deep nesting; duplicate keys; `${var}` substitution; skip resolution |

## Constraints

- C++17 only; `std::filesystem` allowed; no third-party JSON.
- Test-only: nothing under `tests/conformance/support/` is included from `src/` or `include/`.
- Naming per `.clang-tidy` (lower_case members, `m_` for private members); 80 columns; the sketch above shows API shape, not formatting.
- Matcher behavior must be byte-for-byte consistent with the Python one on all vectors.

## Success criteria

1. Self-test passes with 100 % of vectors from subtask 02.
2. `mini_json` rejects all 20 malformed inputs without exceptions escaping.
3. No production target gains a dependency on `conformance_support`.
4. Adding a new adapter requires implementing only `IConformanceAdapter` (demonstrated by subtask 04 being the sole addition).

## Out of scope / risks

- Risk: ~800 lines of test infrastructure; keep every file under 400 lines, split if exceeded.
- Parsing the *request* into a production `CommandRequest` is the shim's job (subtask 04), not this library's.
