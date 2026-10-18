# 09 - API reference regeneration

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** Task 04.0 (new routes registered), Task 07.0 subtask 17 (drift test)
**Blocks:** 17
**Estimated size:** S
**Suggested agent:** docs-updater

## Objective

Regenerate `docs/api/README.md` from `RouteRegistry` so it lists the new routes
(`GET /api/v1/links`, `POST /api/v1/commands`, `GET /api/v1/commands`,
`GET /api/v1/jobs`, `GET /api/v1/jobs/{id}`, `POST /api/v1/jobs/{id}/cancel`),
and add the human-written cross-reference that the generator output cannot hold.

## Dependencies

- `tools/route_registry_dump.cpp` (target `route_registry_dump`) - the generator; `tests/unit/docs/05_api_reference_matches_registry.cpp` (07.0 subtask 17) fails if the file is stale.
- Generated header text lives in the tool; hand-editing the generated file is forbidden.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/api/README.md` | modify (by running the generator) | new rows; no manual edits |
| `tools/route_registry_dump.cpp` | **not** modified here | if the header text needs a "Command endpoints" pointer, request it from Task 04.0 (code change) |

## Classes and functions (procedure)

1. `cmake --build cmake-build --target route_registry_dump`.
2. Run it and write the output over `docs/api/README.md` (command as documented in the tool; PowerShell example below).
3. Review the diff: only added rows for the six routes (and any intended operation-id/summary changes); the existing rows are byte-identical (contract C3). Any change to an existing row is a defect to send back to 04.0.
4. Confirm each new row has a summary, tags, parameters, and the `Notes` column markers consistent with existing rows; confirm `compatibility alias`/`placeholder` markers are untouched.
5. Cross-reference: if the generator header cannot carry it, add the pointer in `docs/commands/README.md` section 2/5 (already links `docs/api/README.md`) rather than editing the generated file.
6. OpenAPI: if the project emits OpenAPI from `RouteRegistry` (CLAUDE.md: "`docs/api/README.md` and the OpenAPI output pick them up"), locate the output file with `git grep -n "openapi" -- src tools docs`; if a checked-in artifact exists, regenerate it too; otherwise note "no checked-in OpenAPI artifact" in the PR.

## Implementation steps

See procedure; finish with `ctest -R "^docs__05_"`.

## Commands

```powershell
cmake --build cmake-build --config Debug --target route_registry_dump
cmake-build\Debug\route_registry_dump.exe | Set-Content -Encoding utf8 docs\api\README.md   # confirm the tool's real invocation first
```

```bash
cmake-build/route_registry_dump > docs/api/README.md
git diff --stat docs/api/README.md
ctest --test-dir cmake-build -R "^docs__0[45]_" --output-on-failure
```

(Windows PowerShell 5.1 `Set-Content -Encoding utf8` writes a BOM; use Bash for generation or `[IO.File]::WriteAllText` with UTF-8 no BOM, and check line endings match the committed file.)

## Tests

`docs__05_api_reference_matches_registry`, `docs__04_command_route_table_consistency` green.

## Constraints

- Generated file only changes by running the generator; no manual edits.
- Line endings/BOM identical to the committed file style.

## Success criteria

1. Diff adds exactly the new routes and nothing else (or documented intended changes).
2. Drift tests green.
3. Links from `docs/cli/README.md` to existing operation ids still resolve (`git grep` each `operation_id` anchor).

## Out of scope / risks

- Changing generator output format. Risk: tool invocation or output location differs from assumption; read `tools/route_registry_dump.cpp` `main` first.
