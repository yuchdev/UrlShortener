# 04 - Response envelope and Job stub

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/02 (and shares `envelope.hpp` with 03; whichever lands second extends the file)
**Blocks:** 06, 08, 04.0/09, 04.0/13, 04.0/15, 05.0/14, 06.0/02
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Add `CommandResponse`, the job stub (`JobState`, `JobInfo`) and the
serialize/parse pair for the response envelope, enforcing "exactly one of
`result`, `job`, `error`". The Job stub is only the *wire shape* of an accepted
job (`{"id","state"}`); the runtime (`JobRunner`, `job.get/list/cancel`) is
Task 05.0. `result` payloads are pre-serialized JSON text produced by handlers
(for link commands: the existing `serializeLinkViewJson` output), so the
envelope never re-encodes them.

## Dependencies

- 01.0/02 `CommandError`, `CommandErrorCode`, `error_info_for`.
- 01.0/01 `scan_object` (used by `parse_response`, which exists for clients,
  stdio consumers and the conformance corpus in Task 07.0).
- Catalog section 4 job states: `queued, running, succeeded, failed, cancelled`.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/envelope.hpp` | modify (create if 03 has not landed) | Append response/job types and functions |
| `src/commands/envelope.cpp` | modify | Serializer and response parser |
| `tests/unit/commands/04_response_serialize.cpp` | create | Serialization and round-trip tests |
| `sources.cmake` | modify | `COMMANDS_UNIT_SOURCES` += test (the `.cpp` is already listed by 03) |

If 04 lands before 03, it creates `envelope.hpp/.cpp` and adds the
`COMMON_CPP_SOURCES` entry; 03 then only extends.

## Classes and functions

```cpp
namespace url_shortener::commands {

enum class JobState { queued, running, succeeded, failed, cancelled };
std::string_view job_state_name(JobState state);                // "queued", ...
std::optional<JobState> job_state_from_name(std::string_view);

struct JobInfo
{
    std::string id;                       // "j-<n>", assigned by the runner (05.0)
    JobState state = JobState::queued;
};

struct CommandResponse
{
    int v = 1;
    std::string id;
    bool ok = false;
    std::string result_json;              // set iff ok && !job: exact JSON text
    std::optional<JobInfo> job;           // set iff ok && accepted as a job
    CommandError error;                   // set iff !ok
};

CommandResponse make_success(std::string id, std::string result_json);
CommandResponse make_job_accepted(std::string id, JobInfo job);
CommandResponse make_failure(std::string id, CommandError error);

/// {"v":1,"id":"...","ok":true,"result":...} etc. Field order fixed:
/// v, id, ok, then result | job | error. Uses url_shortener::jsonString() for
/// escaping id and messages (called from the .cpp only).
std::string serialize_response(const CommandResponse& response);

/// Inverse of serialize_response, for clients and the conformance harness.
struct parse_response_result { std::optional<CommandResponse> response; std::string error; };
parse_response_result parse_response(std::string_view text);

/// HTTP status for the generic endpoint: 200 for ok (including job accepted),
/// else error_info_for(code).http_status.
unsigned http_status_for(const CommandResponse& response);
/// CLI exit code: 0 for ok, else error_info_for(code).exit_code.
int exit_code_for(const CommandResponse& response);
}
```

Invariants (asserted in `serialize_response`, violation -> the function returns
a well-formed `internal` failure envelope rather than invalid JSON):
exactly one of result/job/error; `ok == true` implies `error.ok()`;
`result_json` must be non-empty and start with `{`, `[`, `"`, digit, `t`, `f` or
`n` (cheap sanity, not full validation; handlers are tested separately).

`serialize_response` places `error` as
`{"code":"<name>","message":"<text>"}`; `job` as `{"id":"..","state":".."}`.

## Implementation steps

1. Append the types to `envelope.hpp`.
2. Implement `job_state_name` / `from_name` with a static table.
3. Implement the three factory functions and `serialize_response` using
   `url_shortener::jsonString` (verified in `core/utils.h`/`utils.cpp`). Note
   that `core/utils.h` includes `boost/beast/http.hpp`, so it may be included
   only from `src/commands/*.cpp`, never from a header under
   `include/url_shortener/commands/` (enforced by subtask 13).
4. Implement `parse_response` via `scan_object` (top-level) and one nested
   `scan_object` for `job` / `error`.
5. Implement `http_status_for` / `exit_code_for` through `error_info_for` only.
6. Tests, sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__04_response_serialize
ctest --test-dir cmake-build -R "^commands__04_response_serialize$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/04_response_serialize.cpp` | `commands__04_response_serialize` | unit | below |

- Byte-exact output for the three examples in catalog section 1 (success, job,
  failure), including field order.
- `result_json` is embedded verbatim (a result containing quotes, unicode and
  nested braces is not re-escaped).
- Message escaping: quote, backslash, newline, control character in
  `error.message` produce valid JSON (re-scanned with `scan_object`).
- Invariant violations (neither/both of result/job/error; ok with error set)
  -> `internal` envelope, never malformed JSON.
- Round trip `parse_response(serialize_response(r))` for success, job
  (all five `JobState`s), and failure for every `CommandErrorCode` except
  `none`.
- `parse_response` rejects: two payload fields, none, unknown `ok` type,
  unknown `error.code` name, unknown job state.
- `http_status_for` / `exit_code_for`: ok -> 200/0; job accepted -> 200/0;
  `not_found` -> 404/1; `busy` -> 503/4; `job_cancelled` -> 200/4.

## Constraints

- Statuses and exit codes come only from the subtask 02 table (task README).
- `error.message` must not contain argument values (callers' responsibility,
  asserted at the registry level in 07); this subtask only escapes.
- No JSON library; strings escaped with the existing `jsonString`.
- Milestone contracts: C2, C1 (result is produced once by the handler).

## Success criteria

1. `commands__04_response_serialize` passes.
2. `serialize_response` output of each catalog example equals the catalog text
   (test compares).
3. `parse_response` is the exact inverse for all codes and job states (loop
   test).

## Out of scope / risks

- Job runtime, ids `j-<n>`, `job.get/list/cancel` (Task 05.0).
- The `job_cancelled` code appears *inside* `job.get` results and as a failure
  only when a caller waits on a cancelled job; both shapes use the same table.
- Streaming/partial responses are not supported (one response per request).
