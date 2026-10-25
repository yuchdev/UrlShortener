# 05 - job.get, job.list, job.cancel commands

**Parent task:** 05.0 Jobs, pair import/export and stdio transport
**State:** ⬜ Not started
**Depends on:** 05.0/01, 05.0/02, 05.0/04
**Blocks:** 11, 20, 06.0/15, 06.0/16, 06.0/17 (Import/Export, Jobs and Dashboard screens use `job.*`)
**Estimated size:** S
**Suggested agent:** cpp-expert (tests: testing-expert)

## Objective

Register the three job inspection commands from catalog section 3.4 on the registry: `job.get` (read), `job.list` (read), `job.cancel` (write), with the result shapes documented there. They are plain `sync` commands over the process-wide `JobRunner`.

## Dependencies

- 05.0/01-02 for the runner API, 05.0/04 so a runner is attached to the registry.
- 01.0/05 `ArgSpec`, 01.0/06 `CommandRegistry::add`, `CommandDescriptor` (names partly UNVERIFIED, see 04).
- `jsonString`/`formatTimestamp` from `core/utils.hpp` (verified), used in the `.cpp` only.

## Files affected

| Path | Action | What changes |
|---|---|---|
| `include/url_shortener/commands/job_commands.hpp` | create | `register_job_commands(CommandRegistry&, JobRunner&)`, `serialize_job_json` |
| `src/commands/job_commands.cpp` | create | three descriptors + handlers + serializer |
| `tests/unit/commands/54_job_commands.cpp` | create | cases below |
| `sources.cmake` | modify | add to `COMMON_CPP_SOURCES` (feeds `url_shortener_common`; the `URL_SHORTENER_CPP` list at the top of the file is legacy): `src/commands/job_commands.cpp`; test into `COMMANDS_UNIT_SOURCES` |

## Classes and functions

```cpp
namespace url_shortener::commands {
void register_job_commands(CommandRegistry& registry, JobRunner& runner);
    // adds job.get, job.list, job.cancel; handlers capture `runner` by reference
    // (the runner is process-wide and outlives every registry, see 20)
std::string serialize_job_json(const JobRecord& record);
}
```

Descriptors (catalog 3.4):

| Command | Perm | Args (ArgSpec) | Result |
|---|---|---|---|
| `job.get` | read | `id` string required | job object |
| `job.list` | read | `state` enum queued\|running\|succeeded\|failed\|cancelled optional; `limit` int 1..500 default 50; `offset` int >=0 default 0 | `{"items":[job...],"total":n}` |
| `job.cancel` | write, mutating | `id` string required | job object (no-op on finished jobs) |

Job object: `{"id","command","state","progress":{"done","total"},"result"?:{...raw...},"error"?:{"code","message"},"created_at","started_at"?,"finished_at"?}`. `result` embeds `result_json` verbatim (already a serialized object); `error` is present for `failed` and for `cancelled` (code `job_cancelled`, per catalog error table: appears in `job.get` with HTTP 200). `started_at` is an additive field. Timestamps: `formatTimestamp` (RFC 3339 UTC).

Errors: unknown id -> `not_found` (message `job not found`; there is no `job_not_found` code, catalog section 1). `args_digest` is never serialized.

## Implementation steps

1. Write `serialize_job_json` with `jsonString` for every string; build by `std::ostringstream`, field order as above.
2. Descriptor `job.get`: handler reads `id`, calls `runner.get`, returns result or `not_found`.
3. `job.list`: parse `state` with `parse_job_state` (ArgSpec enum already rejects bad values; defensive nullopt -> `invalid_field`), call `runner.list`.
4. `job.cancel`: `runner.cancel(id)`; nullopt -> `not_found`.
5. Register in a single function; the commands reach the generated catalog block (01.0/11-12) automatically through the registry; `build_command_runtime` calls it (05.0/20).
6. Tests build a registry with a real runner and gated bodies.

## Commands

```text
cmake --build cmake-build --target commands__54_job_commands
ctest --test-dir cmake-build -R "^commands__54_job_commands$" --output-on-failure
```
Windows/multi-config: `--config Debug` / `-C Debug`.

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/54_job_commands.cpp` | `commands__54_job_commands` | unit | `job.get` of queued/running/succeeded/failed/cancelled job shapes (field presence per state: `result` only when set, `error` for failed and cancelled, `finished_at` only when terminal); `job.get` unknown id -> `not_found`; `job.list` default limit, state filter, offset beyond total, limit 501 and 0 -> `invalid_field`, unknown state -> `invalid_field`; `job.cancel` queued, running (state still `running`, later `cancelled`), finished (no-op, same record), unknown -> `not_found`; `ControlSet::User` may `job.get`/`job.list` but `job.cancel` -> `not_authorized`; job JSON never contains the args digest or arg values; `result` is embedded unescaped as an object (valid nesting) |

Manual check: none.

## Constraints

- C1: one handler per command; REST/CLI/TUI/stdio reuse them.
- C2: shapes exactly as the catalog; additive fields only.
- C7: permission from the descriptor, not from handler code.
- C6: no args, file paths beyond what the handler put into `result_json`, or digests in output.
- C++17, no JSON library, `.cpp` in `sources.cmake`, naming per `.clang-tidy`.

## Success criteria

1. `commands__54_job_commands` passes.
2. `command.list` (01.0) shows the three commands with perms `read`, `read`, `write` (extend the 01.0 catalog test or assert in this test).
3. Catalog drift test (07.0) can diff `docs/commands/README.md` section 3.4 against these descriptors: names and perms match.

## Out of scope / risks

- REST routes `/api/v1/jobs*`: subtask 11.
- Streaming progress / push events: polling only (TUI polls `job.get`).
- Risk: embedding `result_json` verbatim trusts handlers to produce valid JSON; the import/export tests assert validity with the existing manual field extractors.
