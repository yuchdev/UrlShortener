# 13 - Error-code coverage and include hygiene

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/08, 01.0/09, 01.0/12
**Blocks:** none (closes Task 01.0; gates Task 03.0)
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Close the task with two guard tests: (1) every error code listed in the
catalog is produced end to end through `CommandRegistry` and survives
serialization, status and exit-code lookup; (2) the public headers of the
commands layer do not include forbidden dependencies. This replaces the former
"core unit tests" subtask: per-feature tests now live in 01-12, this one holds
the cross-cutting guarantees.

## Dependencies

- All earlier subtasks of 01.0 (needs registry options, audit and the table).
- `SOURCE_DIR` compile definition mechanism (see subtask 12).
- Catalog error table in `docs/commands/README.md` section 1.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `tests/unit/commands/13_error_code_coverage.cpp` | create | Coverage test |
| `tests/unit/commands/14_include_hygiene.cpp` | create | Header scan test |
| `sources.cmake` | modify | `COMMANDS_UNIT_SOURCES` += both tests |
| `CMakeLists.txt` | modify | `target_compile_definitions(commands__14_include_hygiene PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")` |

(Two test files, one focused change set: "guards". If the reviewer prefers one
commit per file, 14 can be split without affecting numbering of other
subtasks.)

## Classes and functions

No production symbols. Test helpers (in the tests only):

```cpp
// 13: drives each code through a registry with canned handlers
struct code_scenario { CommandErrorCode code; std::function<CommandResponse(CommandRegistry&)> run; };

// 14: returns the #include lines of every file under include/url_shortener/commands/
std::vector<std::pair<std::string, std::string>> collect_includes(const std::filesystem::path& dir);
```

Scenarios for 13 (how each code is produced):

| Code | Produced by |
|---|---|
| `invalid_envelope` | `execute_json` with malformed text |
| `unknown_command` | unregistered name |
| `invalid_field` | bad arg |
| `not_authorized` | `User` calling a write command |
| `unsupported` | `dry_run` on a non-supporting command; job without submitter |
| `busy` | fake submitter returns `busy` |
| `job_cancelled` | handler of a fake `job.get` returns it; also serialized with HTTP 200 / exit 4 |
| `invalid_url`, `invalid_slug`, `reserved_slug`, `slug_conflict`, `not_found`, `storage_failure` | fake handler returns each `app::AppErrorCode` through `from_app_error` |
| `internal` | handler throws |

## Implementation steps

1. Write 13 as a table-driven test iterating `all_error_infos()` (skipping
   `none`) and failing if any code has no scenario - so adding a code forces
   adding a scenario.
2. For each response: check `error.code`, `serialize_response` +
   `parse_response` round trip, `http_status_for`, `exit_code_for` equal the
   table row.
3. Write 14: read all `*.hpp` under `<SOURCE_DIR>/include/url_shortener/commands/`,
   extract `#include` lines, fail on any forbidden entry.
4. Register, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__13_error_code_coverage commands__14_include_hygiene
ctest --test-dir cmake-build -R "^commands__1[34]_" --output-on-failure
# whole task:
ctest --test-dir cmake-build -R "^commands__" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/13_error_code_coverage.cpp` | `commands__13_error_code_coverage` | unit | one case per code (14) plus "scenario table covers every enumerator" |
| `tests/unit/commands/14_include_hygiene.cpp` | `commands__14_include_hygiene` | unit | forbidden include list; allowed list sanity |

Forbidden in any header under `include/url_shortener/commands/`:
`boost/beast`, `boost/asio`, `boost/program_options`, `ftxui`,
`url_shortener/http/`, `url_shortener/storage/`, `url_shortener/composition/`,
`url_shortener/core/utils.h` (it pulls in Beast), `url_shortener/cli/`,
`url_shortener/tui/`, `<regex>`. Allowed: std headers,
`url_shortener/commands/`, `url_shortener/app/app_error.hpp`,
`url_shortener/security/control_set.hpp`,
`url_shortener/observability/logger.hpp`.

Boundary cases: a header with `#include <boost/beast/http.hpp>` in a temp
directory fed to the same scanning function fails (negative test of the
checker itself).

## Constraints

- Tests may include any header; the hygiene rule is about public headers only
  (`.cpp` files in `src/commands/` may use `core/utils.h` and `app/`).
- No dependence on network, time or ordering.
- Milestone contracts: C2 (all codes in the envelope), C8-style layering for
  the core, C9.
- Keep the forbidden list in one array in the test with a comment pointing to
  the task README.

## Success criteria

1. `ctest --test-dir cmake-build -R "^commands__" --output-on-failure` is green
   (14 test executables, `commands__01_...` to `commands__14_...`).
2. Removing any scenario (or adding an enumerator to `CommandErrorCode`) makes
   test 13 fail.
3. Adding `#include <boost/asio.hpp>` to a commands header makes test 14 fail.
4. Full unit suite regression-free: `ctest --test-dir cmake-build -L unit`.

## Out of scope / risks

- Behavioral coverage of real link commands (Task 03.0) and cross-transport
  replay (Task 07.0).
- The hygiene test is textual; it cannot see transitive includes. It
  compensates by banning `core/utils.h` explicitly.
- Test numbering: 13 and 14 exist as files while subtask numbering stops at 13
  (this subtask owns both files).
