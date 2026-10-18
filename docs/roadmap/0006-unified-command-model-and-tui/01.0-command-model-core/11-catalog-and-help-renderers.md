# 11 - Catalog and help renderers

**Parent task:** 01.0 Command model core
**State:** ⬜ Not started
**Depends on:** 01.0/10
**Blocks:** 12, 04.0/13
**Estimated size:** S
**Suggested agent:** cpp-expert

## Objective

Add two pure renderers over the registry: Markdown for the command catalog
(the generated counterpart of `docs/commands/README.md` section 3) and plain
text help for one command (the body that CLI `cmd <name> --help` and the TUI
help pane will print). They are library functions with unit tests; the tool
that writes them to disk is subtask 12. Mirrors how `RouteRegistry` data is
rendered into `docs/api/README.md` by `tools/route_registry_dump.cpp`.

## Dependencies

- 01.0/10 (`arg_type_name`, `permission_name`, `kind_name`, a registry with a
  real descriptor).
- 01.0/06 `CommandDescriptor::maps_to` for the "Maps to" lines.
- Pattern reference: `tools/route_registry_dump.cpp` and
  `tests/unit/docs/02_route_registry_docs.cpp` (verified; the latter renders
  Markdown inside the test rather than diffing the file).

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `include/url_shortener/commands/catalog.hpp` | create | Renderer declarations, marker constants |
| `src/commands/catalog.cpp` | create | Implementation |
| `tests/unit/commands/11_catalog_render.cpp` | create | Tests |
| `sources.cmake` | modify | `COMMON_CPP_SOURCES` += `src/commands/catalog.cpp`; `COMMANDS_UNIT_SOURCES` += test |

## Classes and functions

```cpp
namespace url_shortener::commands {

inline constexpr std::string_view catalog_begin_marker =
    "<!-- BEGIN GENERATED: commands (tools/command_catalog_dump) -->";
inline constexpr std::string_view catalog_end_marker =
    "<!-- END GENERATED: commands -->";

/// Deterministic Markdown: an index table (Command | Perm | Kind | Mut |
/// dry_run | Summary) followed by one block per command in name order:
///   #### `link.create` - Perm `write`, sync, Mut, `dry_run` yes
///   <summary>
///   | Arg | Type | Req | Description |
///   ...
///   Maps to: `POST /api/v1/links`, `link create`.
/// LF line endings only, no timestamps, no trailing spaces, '|' in doc text
/// escaped as '\|'. The returned text does NOT include the markers.
std::string render_catalog_markdown(const CommandRegistry& registry);

/// Help body for one command:
///   Usage: url_shortener cmd <name> [--json '<args-object>' | --arg key=value ...]
///   <summary>
///   Arguments:
///     --arg <name>   <type> (required|optional) [enum a|b] [default x]  <doc>
///   Permission: <perm>; mutating: yes|no; dry_run: yes|no; kind: sync|job
std::string render_command_help(const CommandDescriptor& descriptor);
}
```

The Type column and arg-type vocabulary come from `arg_type_name` (10) so the
catalog, `command.list` and help use the same words. An `one_of_group` arg is
rendered as `required (one of: slug, id)` consistent with the catalog's
"one of" wording.

## Implementation steps

1. Header with marker constants and Doxygen.
2. Implement the two renderers with a tiny `markdown_escape` helper.
3. Tests with a fixture registry containing a representative set (sync read,
   mutating with dry-run, job, enum arg, one_of_group, object arg, no-arg
   command).
4. Sources, build, run.

## Commands

```powershell
cmake --build cmake-build --target commands__11_catalog_render
ctest --test-dir cmake-build -R "^commands__11_catalog_render$" --output-on-failure
```

## Tests

| Test file | Target | Label | Cases |
|---|---|---|---|
| `tests/unit/commands/11_catalog_render.cpp` | `commands__11_catalog_render` | unit | below |

- Golden comparison: the fixture registry renders to an exact expected string
  embedded in the test (covers headings, tables, "Maps to").
- Determinism: render twice, and render after inserting the same descriptors in
  reverse order -> identical bytes.
- Escaping: a doc containing `|` and a backtick does not break the table row.
- Empty registry renders only the (empty) index header without crashing.
- `render_command_help`: required vs optional wording, enum values, default,
  `one_of` group, and the footer line; a command with no args prints
  `Arguments: (none)`.
- No `\r`, no tab, no trailing whitespace in either output (loop assertion).
- Output never contains handler addresses or any field not in the descriptor.

## Constraints

- Pure functions of the registry: no clocks, no environment, no locale (output
  must be identical on Windows and Linux CI).
- Source of truth is the C++ registry (C1); hand edits inside the generated
  block in `docs/commands/README.md` are rejected by the drift test (12).
- C++17; no JSON library; headers free of Beast/storage includes.

## Success criteria

1. `commands__11_catalog_render` passes.
2. Golden and determinism tests pass on both Windows (MSVC) and Linux
   (verified by CI matrix, not locally on one OS only).
3. Output of `render_command_help` for the fixture contains every `ArgSpec`
   name exactly once (test).

## Out of scope / risks

- Writing files or touching `docs/commands/README.md` (12).
- Compiling CLI sugar flags (`link create --url ...`) from `ArgSpec` and the
  per-verb `--help` of `cli_parser.cpp` (Task 04.0 subtasks 14-15); this subtask
  only provides the generic `cmd <name>` help body.
- Section 5 (REST route -> command table) cannot be generated until Task 04.0
  introduces the route table; see subtask 12.
