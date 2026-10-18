# 02 - Error-code / status / exit-code table

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** none
**Blocks:** 03, 04, 05, 03.0/05, 04.0/02, 04.0/15
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Define `CommandErrorCode`, `CommandError` and the one table that maps every
error code to its wire name, HTTP status and CLI exit code, plus the mapping
from `app::AppErrorCode`. This is "the single status/exit-code table": after
this subtask no other file may contain such a mapping for commands (REST and CLI
adapters read it in Task 04.0; the existing mappings in
`link_handlers.cpp::statusForAppError` and
`link_command_dispatch.cpp::ExitCodeForAppError` are only cross-checked by tests
here, not edited).

## Dependencies

- `include/url_shortener/app/app_error.hpp` (`app::AppErrorCode`, verified:
  `none, invalid_url, invalid_slug, reserved_slug, slug_conflict,
  invalid_field, not_found, storage_failure, internal`).
- [docs/commands/README.md](/docs/commands/README.md) section 1 "Error codes".
- `url_shortener::cli::ExitCodeForAppError` (`src/cli/link_command_dispatch.cpp`,
  verified, in `COMMON_CPP_SOURCES`) is linked by the test to prove parity.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/error_codes.hpp` | create | Enum, struct, lookup API |
| `src/commands/error_codes.cpp` | create | The table and lookups |
| `tests/unit/commands/02_error_table.cpp` | create | Table and parity tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/commands/error_codes.cpp`; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

`namespace url_shortener::commands`:

```cpp
enum class CommandErrorCode
{
    none,
    invalid_envelope, unknown_command, invalid_field, invalid_url,
    invalid_slug, reserved_slug, slug_conflict, not_found, not_authorized,
    unsupported, busy, job_cancelled, storage_failure, internal
};

struct CommandError
{
    CommandErrorCode code = CommandErrorCode::none;
    std::string message;          // human text; never contains arg values
    bool ok() const { return code == CommandErrorCode::none; }
};

struct error_info
{
    CommandErrorCode code;
    std::string_view name;            // wire value of error.code
    unsigned http_status;             // generic POST /api/v1/commands
    int exit_code;                    // CLI `cmd`
    unsigned legacy_rest_status;      // existing resource routes today
    std::string_view legacy_rest_code;// existing resource-route "error" string
};

const std::vector<error_info>& all_error_infos();            // table order = enum order
const error_info& error_info_for(CommandErrorCode code);
std::optional<CommandErrorCode> error_code_from_name(std::string_view name);
CommandErrorCode from_app_error(app::AppErrorCode code);     // exhaustive switch, no default
CommandError make_error(CommandErrorCode code, std::string message);
```

The table (authoritative values; first four value columns come from the
catalog except where marked, the legacy columns are copied from
`statusForAppError` / `codeForAppError` in `src/http/handlers/link_handlers.cpp`
and `ExitCodeForAppError`):

| name | http | exit | legacy REST status / code |
|---|---|---|---|
| `invalid_envelope` | 400 | 2 | 400 / `invalid_envelope` |
| `unknown_command` | 404 | 2 | 404 / `unknown_command` |
| `invalid_field` | 400 | 2 | 400 / `invalid_request` |
| `invalid_url` | 400 | 2 | 400 / `invalid_url` |
| `invalid_slug` | 400 | 2 | 400 / `invalid_slug` |
| `reserved_slug` | **409** | 2 | 409 / `reserved_slug` |
| `slug_conflict` | 409 | 3 | 409 / `slug_conflict` |
| `not_found` | 404 | 1 | 404 / `not_found` |
| `not_authorized` | 403 | 5 | 403 / `not_authorized` |
| `unsupported` | 501 | 2 | 501 / `unsupported` |
| `busy` | 503 | 4 | 503 / `busy` |
| `job_cancelled` | 200 | 4 | 200 / `job_cancelled` |
| `storage_failure` | 500 | 4 | 500 / `repository` |
| `internal` | 500 | 4 | 500 / `internal_error` |

`from_app_error`: `none->none`, each same-named code maps to itself.

Spec inconsistency recorded here: the catalog lists `reserved_slug` as HTTP
**400**, but `statusForAppError` returns **409** for `reserved_slug`
(verified in `link_handlers.cpp`). Milestone contract C3 (REST statuses do not
change) wins, so the table uses 409; the catalog row has been corrected on
this branch and Task 08.0 subtask 03 re-checks it against the code. The catalog's exit code for
`reserved_slug` (2) matches `ExitCodeForAppError`.

The `legacy_*` columns exist because resource routes keep today's strings
(`invalid_request`, `repository`, `internal_error`) while the generic endpoint
and the envelope use the AppErrorCode names (catalog: "existing resource routes
keep their current per-field 400 codes"). They can be dropped when the
resource routes are retired; they are in the same table so there is still one
place.

## Implementation steps

1. Write the header with Doxygen on every public symbol.
2. Implement the table as a `static const std::vector<error_info>` built in
   enum order; `error_info_for` indexes by `static_cast<size_t>(code)`
   (assert the table order in the test, not at runtime).
3. Implement `error_code_from_name` (linear scan, table is tiny).
4. Implement `from_app_error` with an exhaustive `switch` and no `default`
   (compile-time prompt when `AppErrorCode` grows, same technique as
   `ExitCodeForAppError`).
5. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__02_error_table
ctest --test-dir cmake-build -R "^commands__02_error_table$" --output-on-failure
```

(Multi-config: `--config Debug` / `-C Debug`.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/02_error_table.cpp` | `commands__02_error_table` | unit | below |

- Table covers every enumerator exactly once, in enum order; names are unique
  and lower_snake_case.
- Name round-trip for all codes: `error_code_from_name(error_info_for(c).name)`;
  unknown name and empty name -> `nullopt`.
- Exit-code parity: for every `app::AppErrorCode a`,
  `error_info_for(from_app_error(a)).exit_code ==
  url_shortener::cli::ExitCodeForAppError(a)`.
- Status parity: expected legacy statuses hard-coded from `statusForAppError`
  (`reserved_slug` 409, `not_found` 404, `storage_failure` 500, `internal` 500,
  `invalid_*` 400, `slug_conflict` 409); asserts `legacy_rest_status`.
- Legacy code strings hard-coded from `codeForAppError`.
- Boundary: `from_app_error(none)` is `none`; `make_error` keeps the message.
- Catalog parity: every error code named in the catalog table appears in the
  table with the same HTTP status, exit code and legacy resource-route string
  (the catalog now lists `reserved_slug` as 409 and has the legacy column).

## Constraints

- One table only (task README constraint); nothing else in `src/commands/`
  may switch on `CommandErrorCode` to produce a status or exit code.
- `message` must never receive argument values; callers pass fixed strings or
  the argument *name* (mirrors the audit rule, milestone C6).
- C++17; no exceptions; header has no storage/http includes.
- Milestone contracts: C1 (single definition), C2, C3 (statuses unchanged).

## Success criteria

1. `commands__02_error_table` passes.
2. The parity tests fail if `ExitCodeForAppError` or the table is edited
   independently (demonstrated by temporarily changing one value locally).
3. The table and the catalog error table agree row by row (no documented
   differences remain).

## Out of scope / risks

- Editing `link_handlers.cpp` / `link_command_dispatch.cpp` to read this table
  (Task 04.0).
- `invalid_timestamp` / `invalid_bucket` (extra `error.code` strings produced
  by `codeForAppError` for stats): not part of the envelope vocabulary; they
  surface as `invalid_field` with the field name in the message in Task 03.0.
  The legacy resource route keeps emitting them unchanged.
- Codes with no legacy equivalent (`invalid_envelope`, `unknown_command`,
  `not_authorized`, `unsupported`, `busy`, `job_cancelled`) repeat the generic
  values in the legacy columns. The existing placeholder routes answer 501
  `feature_not_enabled`; that is a route-level string, not an AppError, and
  stays untouched.
