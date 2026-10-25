# 01 - Job types and JobStore

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** none (pure data structure; uses `IClock` from `core/clock.hpp`)
**Blocks:** 02, 05
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Define the job vocabulary (`JobState`, `JobProgress`, `JobFailure`, `JobRecord`) and a thread-safe in-memory `JobStore` that owns the record table, the legal state transitions, id allocation (`j-<n>`) and bounded retention of finished jobs. No threads, no queue, no command code: this is the data model that `JobRunner` (02) mutates and `job.*` (05) reads.

## Dependencies

- `IClock` / `ManualClock` in `include/url_shortener/core/clock.hpp` (verified; global namespace) for deterministic timestamps.
- No dependency on 01.0: the store deliberately uses its own `JobFailure {code, message}` instead of the envelope's error type so that it stays free of envelope headers; subtask 04/05 convert.
- `include/url_shortener/commands/` must not include Beast (01.0 constraint). `core/utils.hpp` pulls `boost/beast/http.hpp`, so these headers must NOT include it; timestamp formatting happens in subtask 05 `.cpp` only.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/job_types.hpp` | create | `JobState`, `JobProgress`, `JobFailure`, `JobRecord`, `JobPage`, string conversions |
| `include/url_shortener/commands/job_store.hpp` | create | `JobStore` class declaration |
| `src/commands/job_store.cpp` | create | implementation (also holds the `JobState` string conversions) |
| `tests/unit/commands/50_job_store.cpp` | create | Boost.Test cases below |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/commands/job_store.cpp`; add the test to `COMMANDS_UNIT_SOURCES` (list created by 01.0 core tests; if absent create it and add `register_labeled_cpp_tests(COMMANDS_UNIT_SOURCES "unit")` next to its siblings in `CMakeLists.txt`) |

Note: CLAUDE.md says test lists live in `CMakeLists.txt`; in this repo they are `set(...)` lists in `sources.cmake`, registered by `register_labeled_cpp_tests` in `CMakeLists.txt`.

## Classes and functions

All in `namespace url_shortener::commands` (NEW namespace; matches 01.0 headers). Type names follow ADR 0002 (CamelCase); methods, members of public structs and enumerators are `lower_case`; private members `m_` prefix.

```cpp
enum class JobState { queued, running, succeeded, failed, cancelled };
std::string_view to_string(JobState state) noexcept;               // "queued"...
std::optional<JobState> parse_job_state(std::string_view) noexcept; // for job.list filter
bool is_terminal(JobState state) noexcept;                          // succeeded|failed|cancelled

struct JobProgress { std::uint64_t done = 0; std::uint64_t total = 0; };
struct JobFailure  { std::string code; std::string message; };      // code = catalog error code string

struct JobRecord {
    std::string id;                  // "j-<n>"
    std::string command;             // "link.import"
    std::string args_digest;         // first 16 hex of SHA-256 of args; never the args (C6)
    JobState state = JobState::queued;
    JobProgress progress;
    std::optional<std::string> result_json;   // pre-serialized object text
    std::optional<JobFailure> failure;
    std::chrono::system_clock::time_point created_at;
    std::optional<std::chrono::system_clock::time_point> started_at;
    std::optional<std::chrono::system_clock::time_point> finished_at;
};
struct JobPage { std::vector<JobRecord> items; std::size_t total = 0; };

class JobStore {
public:
    JobStore(const IClock& clock, std::size_t max_retained);          // max_retained >= 1 else std::invalid_argument
    JobRecord create(std::string command, std::string args_digest);   // state queued, new id
    bool mark_running(const std::string& id);                         // queued -> running only
    bool update_progress(const std::string& id, JobProgress progress);// running only; done must not decrease
    bool finish(const std::string& id, JobState terminal,
                std::optional<std::string> result_json,
                std::optional<JobFailure> failure);                   // queued|running -> terminal; false if already terminal/unknown
    std::optional<JobRecord> get(const std::string& id) const;
    JobPage list(std::optional<JobState> filter, std::size_t offset, std::size_t limit) const; // newest first
    std::size_t active_count() const;                                 // queued + running
private:
    void evict_locked();                                              // drop oldest terminal jobs beyond m_max_retained
    const IClock& m_clock; const std::size_t m_max_retained;
    mutable std::mutex m_mutex; std::uint64_t m_next_id = 1;
    std::deque<JobRecord> m_records;                                  // insertion order = creation order
};
```

Error handling: expected conditions (unknown id, illegal transition) return `false`/`std::nullopt`; only the constructor throws (`std::invalid_argument`) for a programming error. `finish` with a non-terminal `terminal` argument is a programming error: `assert` plus return `false`.

## Implementation steps

1. Create `job_types.hpp` with the enums/structs; keep it free of `<boost/...>` and `utils.hpp`.
2. Implement `to_string`/`parse_job_state`/`is_terminal` in `job_store.cpp`.
3. Implement `JobStore` under a single `std::mutex`; `create` stamps `created_at = m_clock.now()`, allocates `"j-" + std::to_string(m_next_id++)`.
4. Transitions: `mark_running` sets `started_at`; `finish` sets `finished_at` and clears nothing else; both reject terminal records. `update_progress` rejects non-running jobs and a decreasing `done`.
5. `evict_locked()` runs at the end of `finish`: while `m_records.size() > m_max_retained`, erase the oldest record whose state is terminal; queued/running records are never evicted (so the table can exceed `max_retained` only by the number of active jobs, which `JobRunner` bounds).
6. `list`: filter by state, newest first, `offset`/`limit` clamp (`limit` 0 returns empty; offset beyond size returns empty with correct `total`).
7. Register sources and the test; build.

## Commands

```text
cmake --build cmake-build --target commands__50_job_store
ctest --test-dir cmake-build -R "^commands__50_job_store$" --output-on-failure
```
Windows/multi-config generators: add `--config Debug` to the build and `-C Debug` to `ctest`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/50_job_store.cpp` | `commands__50_job_store` | unit | create allocates `j-1`, `j-2` and `queued`; legal path queued->running->succeeded; queued->cancelled directly; every illegal transition (terminal->anything, running->queued) returns false and leaves the record untouched; unknown id returns false/nullopt; progress rejects decrease and non-running; timestamps come from `ManualClock` (advance 5 s between steps and assert `started_at`/`finished_at`); retention: with `max_retained=3` and 5 finished jobs only the 3 newest remain, active jobs are never evicted; `list` filter by state, newest-first order, offset/limit boundaries (0, exact size, beyond size); `parse_job_state("bogus")` is nullopt; concurrent `create` from 8 threads yields unique ids (run under the normal test, no sleeps) |

Manual check: none.

## Constraints

- C1 (one implementation) and C6: records store `args_digest`, never raw args.
- C9: no JSON library; `result_json` is opaque text produced by command handlers.
- C++17 only; `lower_case` identifiers, `m_` private members; 4-space indent, 80 columns, braces on own line for class/function/enum/struct definitions.
- Every new `.cpp` listed in `sources.cmake`.
- No effect on the redirect fast path (C10): nothing here is reachable from `GET /{slug}`.

## Success criteria

1. `commands__50_job_store` passes (`ctest -R "^commands__50_job_store$"`).
2. `grep -rn "boost\|utils.hpp" include/url_shortener/commands/job_types.hpp include/url_shortener/commands/job_store.hpp` returns nothing.
3. Illegal transitions are rejected: asserted by the test cases above.
4. The full `unit` label is still green: `ctest --test-dir cmake-build -L unit --output-on-failure`.

## Out of scope / risks

- Durable jobs (non-goal, decision Q8): records vanish on restart.
- Retention is count-based only; no age-based expiry. Revisit if the TUI Jobs screen needs it.
- `std::deque` linear lookup by id is fine for `max_retained` in the hundreds; switch to an index map only if the default grows.
