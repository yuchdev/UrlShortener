# 16 - Catalog drift: command sections

**Parent task:** 07.0 Tests and conformance
**State:** ⬜ Not started
**Depends on:** Tasks 01.0 (catalog generator), 03.0 (all descriptors registered)
**Blocks:** 34, 08.0/03
**Estimated size:** S
**Suggested agent:** testing-expert

## Objective

Fail the unit suite whenever the **hand-written** sections 1-6 of
`docs/commands/README.md` disagree with the live `CommandRegistry` and the code
constants (headings, permission/kind/mutating columns, argument tables, error
table, envelope fields). The generated block `### 3.7` is owned by Task 01.0
subtask 12 (`commands__12_catalog_drift`, byte-for-byte); this test does not
repeat that check (decision log D9, 07.0 README "Drift-test ownership"). This is the
executable form of "docs cannot drift from behavior" (ADR 0002 Consequences).

## Dependencies

- 01.0 catalog renderers (`render_catalog_markdown`, marker constants, 01.0/11) and the `command.list` built-in (01.0/10); the registry is built with `composition::build_command_runtime` (03.0/18, extended by 05.0/20) over the in-memory backend.
- 03.0 so the registry contains all commands; before that the test compares only the commands registered at the time (the generator output is the baseline, so it stays green during incremental development).
- Existing precedent: `tests/unit/docs/01_configuration_examples_match_parser.cpp` (reads repo files via `SOURCE_DIR`) and `tests/unit/docs/02_route_registry_docs.cpp` (renders route markdown in-test).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `tests/unit/docs/03_command_catalog_matches_registry.cpp` | create | Boost.Test drift checks |
| `sources.cmake` | modify | Append to `DOCS_UNIT_SOURCES` |
| `CMakeLists.txt` | modify | After `register_labeled_cpp_tests(DOCS_UNIT_SOURCES "unit")`, add `target_compile_definitions(docs__03_command_catalog_matches_registry PRIVATE SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")` (same line pattern as for `docs__01_...`) |
| `docs/commands/README.md` | modify (only by running the generator when drift is real) | Never hand-edited generated regions |

Target name: `docs__03_command_catalog_matches_registry` (`<parent_dir>__<stem>`), label `unit`.

## Classes and functions

Test cases (all in one file, fixture `CatalogFixture` building a registry with
stub dependencies - handlers are never executed):

```cpp
struct CatalogFixture {
    CatalogFixture();                                  // registry from composition::build_command_runtime (the one production builder), in-memory backend
    url_shortener::commands::CommandRegistry registry;
    std::string catalog_text;                          // read from SOURCE_DIR "/docs/commands/README.md"
};
std::string extractGeneratedRegion(const std::string& md, std::string_view begin, std::string_view end);
```

| Case | Assertion |
|---|---|
| `generated_block_present` | the catalog contains exactly one begin/end marker pair (the byte-for-byte comparison of its content is `commands__12_catalog_drift`, 01.0/12) |
| `every_descriptor_documented` | for each `registry.list()` name, catalog has a `#### \`name\`` heading or a row in the 3.4/3.5 tables |
| `no_undocumented_catalog_commands` | each `#### \`x.y\`` heading in section 3 is a registered descriptor (no reserved command names exist in Milestone 0006; `auth.*`/`user.*` arrive with Milestone 0002 Task 07.0) |
| `error_table_matches_code` | every code of the 01.0 error enum appears in the catalog error table with the same HTTP status and CLI exit; and vice versa (this also pins the Python `HTTP_STATUS`/`CLI_EXIT` maps: the test additionally parses `tests/conformance/py/conformance_common.py` constants and compares) |
| `envelope_fields_documented` | request/response field names in section 1 equal the names emitted by the 01.0 serializers on a sample envelope |
| `perm_kind_mutating_columns_match` | per command, `Perm`, `Kind`, `Mut`, `dry_run` in the catalog equal the descriptor |
| `argspec_tables_match` | parse the 3.1-3.6 `####` headings (`Perm` = the first backticked permission = the base permission; an optional parenthetical `(`write` with `path`)` must correspond to a descriptor with `permission_for` set (D16), and its absence to an unset hook; sync/job, `Mut`, `dry_run`) and arg-table rows (name, type, required, enum values incl. `expired` for `link.list.status`) and compare with the descriptors |

## Implementation steps

1. Use 01.0/11's marker constants (`catalog_begin_marker`/`catalog_end_marker`); do not invent markers here.
2. Write the fixture around the production registration function; assert registry size > 0 so the test cannot pass vacuously.
3. Implement cases; for text diff, report the first differing line number and both lines.
4. Add the CMake definition line.

## Commands

```bash
cmake --build cmake-build --target docs__03_command_catalog_matches_registry
ctest --test-dir cmake-build -R "^docs__03_command_catalog_matches_registry$" --output-on-failure
# this test has no generator: fix the prose by hand. The generated block 3.7 is rewritten by the
# 01.0/12 tool (`command_catalog_dump` prints only the marked block; paste it between the markers).
```

## Tests

| Test file | Target | CTest label | Cases |
|---|---|---|---|
| `tests/unit/docs/03_command_catalog_matches_registry.cpp` | `docs__03_command_catalog_matches_registry` | `unit` | 7 cases above |

## Constraints

- Line endings: compare after normalizing `\r\n` to `\n` (repo has CRLF warnings on Windows checkouts).
- No network, no process spawn; pure unit test.
- Hand-written prose outside generated markers is not compared.

## Success criteria

1. Editing any descriptor field or doc table without regenerating makes the test fail with the regenerate hint.
2. The test fails if a descriptor lacks a catalog entry.
3. Green on a clean tree on Windows and Linux.

## Out of scope / risks

- Route table (section 5) is subtask 17.
- Risk: if 01.0 decides to generate the whole file (not just regions), `extractGeneratedRegion` degenerates to whole-file equality; keep both code paths.
