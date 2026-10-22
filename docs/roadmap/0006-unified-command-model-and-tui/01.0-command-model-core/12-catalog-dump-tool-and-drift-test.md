# 12 - Catalog dump tool and drift test

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/11
**Blocks:** 13
**Estimated size:** S
**Suggested agent:** cpp-expert (docs edit: docs-updater)

> Ownership (decision log D9): this subtask's test is the only byte-for-byte
> check of the generated block `### 3.7`; the hand-written sections 1-6 are
> drift-tested by Task 07.0 subtask 16.

## Objective

Add the `command_catalog_dump` executable (same pattern as
`route_registry_dump`), mark a generated region in `docs/commands/README.md`,
and add a unit test that fails when the committed region differs from what the
registry renders. This delivers "generator output is deterministic; a check
fails on drift".

## Dependencies

- 01.0/11 `render_catalog_markdown`, marker constants.
- Existing pattern (verified): `tools/route_registry_dump.cpp` +
  `add_executable(route_registry_dump ...)` in `CMakeLists.txt` (lines 122-127)
  linking `url_shortener_common`; and the `SOURCE_DIR` compile definition used
  by `docs__01_configuration_examples_match_parser`
  (`CMakeLists.txt` line 184) to let a test read repo files.
- No CI workflow currently diffs `docs/api/README.md` (UNVERIFIED beyond the
  four workflows in `.github/workflows/`); drift protection here is the CTest
  unit test, which the existing `ubuntu-unit-tests.yml` runs via `-L unit`
  (UNVERIFIED exact invocation).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `tools/command_catalog_dump.cpp` | create | `main()` builds a registry and prints the generated block |
| `include/url_shortener/commands/builtin_commands.hpp`, `src/commands/builtin_commands.cpp` | modify | Add `register_documented_commands` |
| `CMakeLists.txt` | modify | `add_executable(command_catalog_dump ...)` beside `route_registry_dump`; `target_compile_definitions(commands__12_catalog_drift PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")` right after the `register_labeled_cpp_tests(COMMANDS_UNIT_SOURCES "unit")` line (added by subtask 01) |
| `tests/unit/commands/12_catalog_drift.cpp` | create | Drift test |
| `docs/commands/README.md` | modify | Add subsection `### 3.7 Generated command reference` containing only the begin/end markers and the generated block (the status banner already names this block as the generated part) |
| `sources.cmake` | modify | `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

Tool (`tools/command_catalog_dump.cpp`):

```cpp
int main(int argc, char** argv);
// Prints catalog_begin_marker, "\n", render_catalog_markdown(registry), "\n",
// catalog_end_marker, "\n" to stdout in binary mode (no CRLF translation on
// Windows). `--check <file>` mode (optional): exits 1 and prints a unified
// hint if the region in <file> differs.
```

The registry is built by one shared function so tool and test cannot diverge.
It is NEW, added to `builtin_commands.hpp` / `builtin_commands.cpp` from
subtask 10 (so no extra file):

```cpp
namespace url_shortener::commands {
/// Registers every descriptor that exists for documentation. In this subtask:
/// register_builtin_commands only. Task 03.0 adds register_link_commands(...),
/// Task 05.0 the job/pair commands, using handlers bound to null services (they
/// must never be invoked by the generator).
void register_documented_commands(CommandRegistry& registry);
}
```

Because this edits the two subtask-10 files, the files table also lists them.

Drift test logic:

1. read `<SOURCE_DIR>/docs/commands/README.md` (skip with a message when
   `SOURCE_DIR` is undefined, like the config-docs test);
2. extract text between the markers (exactly one begin and one end marker, in
   order, else fail with a clear message);
3. normalize CRLF to LF on the file side only;
4. compare with `render_catalog_markdown` of a registry built by
   `register_documented_commands`; on mismatch fail and print the command to
   regenerate:
   `cmake --build cmake-build --target command_catalog_dump` then run it and
   paste into the region.

## Implementation steps

1. Add `register_documented_commands` to the builtin files.
2. Write the tool and CMake target.
3. Run the tool, paste the output into `docs/commands/README.md` under the new
   `### 3.7` heading (hand-written sections 3.1-3.6 stay as narrative until
   Task 08.0; the banner says which block is generated).
4. Write the drift test; wire `SOURCE_DIR`.
5. Build, run, then mutate a summary string locally to see the test fail.

## Commands

```powershell
cmake -S . -B cmake-build -G Ninja
cmake --build cmake-build --target command_catalog_dump commands__12_catalog_drift
./cmake-build/command_catalog_dump > $env:TEMP\catalog_block.md      # inspect, then paste into the doc
ctest --test-dir cmake-build -R "^commands__12_catalog_drift$" --output-on-failure
```

(Visual Studio generators put the executable under `cmake-build/Debug/`.
Redirecting with PowerShell 5.1 `>` writes UTF-16/BOM; use
`Out-File -Encoding utf8` or run through Bash. The tool's own binary-mode
stdout avoids CRLF; the redirect step is the Windows hazard.)

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/12_catalog_drift.cpp` | `commands__12_catalog_drift` | unit | markers present exactly once and ordered; region equals generated output; CRLF-checked-out file still passes; missing markers fails with the regeneration hint; (negative, in-test) comparing against a registry with one extra descriptor fails |

Manual check: run `command_catalog_dump` twice and `fc`/`diff` the outputs
(determinism); open `docs/commands/README.md` and confirm the narrative
sections 3.1-3.6 are unchanged.

## Constraints

- Generator output is deterministic and platform independent (11).
- Hand-edits inside the markers must fail the test; hand-written prose outside
  the markers is untouched.
- The tool must not construct services, open files other than stdout, or touch
  storage; handlers are never invoked.
- Adding the executable does not add it to `url_shortener_common` or the
  server binary.
- Milestone contracts: C1 (code is source of truth), C9.

## Success criteria

1. `commands__12_catalog_drift` passes on a clean tree and fails after any
   edit of a descriptor summary without regenerating (demonstrated manually).
2. `command_catalog_dump` output is byte-identical across two runs and across
   Windows/Linux (CI).
3. The status banner of `docs/commands/README.md` names the generated block
   and the regeneration command.

## Out of scope / risks

- **Spec inconsistency:** the original spec said the generator rewrites
  "sections 3 and 5". Section 5 (REST route -> command table) needs route data
  that only exists after Task 04.0, and the hand-written sections 3.1-3.6 carry
  prose (examples, PATCH semantics) that is not derivable from `ArgSpec`.
  This subtask therefore generates only the new block 3.7. Task 04.0 extends the
  tool for section 5; Task 08.0 decides whether 3.1-3.6 are folded into the
  generated reference or stay as prose with a link.
- Until Task 03.0 registers the link commands, the generated block lists only
  `command.list`.
- Drift detection only runs where unit tests run; there is no separate CI
  diff step (could be added to `ubuntu-unit-tests.yml`).
