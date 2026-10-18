# 05 - ARCHITECTURE.md update

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 03
**Blocks:** 17
**Estimated size:** M
**Suggested agent:** docs-updater

## Objective

Bring `ARCHITECTURE.md` in line with the shipped system and repair a
pre-existing defect: its header table links five ADR files
(`0001-system-architecture`, `0002-backend-topology`, `0003-performance-contract`,
`0004-security-model`, `0005-sql-persistence-soci`) that do not exist in
`docs/adr/` and never existed in git history, and whose numbers now collide
with the real ADR 0001-0003.

## Dependencies

- Subtasks 01-03; outcomes of Tasks 02.0 (storage), 04.0-06.0 (transports, jobs, TUI).
- Existing structure of `ARCHITECTURE.md`: header ADR table; `## 1) High-level architecture` (components bullets, numbered data flow); `## 2) Source layout`; `## 3) Runtime components` (3.1 entrypoint, 3.2 server, 3.3 request handling, 3.4 storage and analytics); `## 4) Build/test system`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `ARCHITECTURE.md` | modify | sections listed below |

## Classes and functions (exact edits)

1. **Header ADR table (lines 3-13):** replace the five dangling rows with the real inventory: ADR 0001 CLI/REST Shared Command Layer, ADR 0002 Unified Command Model, ADR 0003 TUI Framework and Architecture, linking `docs/adr/000N-*.md`. Add one sentence: "Earlier references to ADRs on system architecture, backend topology, performance contract, security model and SOCI persistence pointed at files that were never committed; their content lives in `docs/backend-selection.md`, `docs/storage/`, `docs/configuration.md`, `docs/security/`." Verify each target exists before writing it; remove any that does not. **Design note:** record this repair in the decision log (the dangling references may have been intended future ADRs).
2. **Section 1 bullets:** add `- **Command model**: ... include/url_shortener/commands/` and `- **Terminal UI**: ... src/tui/ (optional, FTXUI)`; extend the Data-flow list: after step 4 add "Resource and generic command routes call `CommandRegistry::execute`; CLI (`cmd`, `link`), `serve-stdio` and the TUI are other transports over the same registry (ADR 0002)". Update step 5 if Task 02.0 replaced `linkRepository()` with the pluggable store.
3. **Section 2 source layout:** add under `include/url_shortener/`: `commands/` (envelope, arg_spec, command_registry, job_runner, transport_adapter, link_commands), `tui/` (command_client); under `src/`: `commands/`, `transport/stdio_adapter.cpp`, `tui/` (screens/), `app/repository_link_store.cpp`; under `tests/`: `conformance/`, `perf/`, `unit/commands`, `unit/tui`; under `docs/`: `commands/`, `tui/`. Use the real file names from the merged tree (`git ls-files`), not plan.md's sketch.
4. **New `### 3.5 Command model and transports`:** envelope, `CommandDescriptor`/`ArgSpec`, `CommandRegistry::execute` order (validate -> authorize -> ArgSpec check -> handler), generated catalog + `command.list`, REST generic/resource routes, `cmd`, stdio, TUI; one diagram copied from ADR 0002 (text form). Link `docs/commands/README.md`, ADR 0002.
5. **New `### 3.6 Jobs`:** `JobRunner`, bounded queue, `busy`, in-memory state, `job.*` commands.
6. **New `### 3.7 Terminal UI`:** FTXUI behind `URLSHORTENER_ENABLE_TUI`, `ICommandClient` seam, no business logic, no listener; link `docs/tui/README.md`, ADR 0003.
7. **Section 3.4 Storage and Analytics:** rewrite the sentence "Domain logic interacts with storage through the `linkRepository()` singleton" per the post-02.0 reality (one configured backend shared by redirects, REST, CLI, TUI); keep the protected-fast-path paragraph in 3.3 and add "The redirect path does not depend on the command layer (enforced by test `commands__80_redirect_isolation_test`)".
8. **Section 4 Build/test:** add: `URLSHORTENER_ENABLE_TUI` option, CTest labels (`unit`, `contract`, `integration`, `e2e`, `perf`, plus `conformance`), conformance corpus under `tests/conformance/commands/`, catalog and API drift tests, perf guard; link `docs/testing/README.md`.

## Implementation steps

1. `git ls-files include src tests docs | head -n 400` to copy real paths; 2. apply edits; 3. `/link-check ARCHITECTURE.md`.

## Commands

```bash
git ls-files include/url_shortener/commands src/commands src/transport src/tui tests/conformance
git log --all --oneline -- docs/adr/0001-system-architecture.md     # expected empty
```

## Tests

None executable; `/link-check ARCHITECTURE.md` clean.

## Constraints

- Keep the file an overview (target < 250 lines); detail stays in ADRs and `docs/`.
- No claims about behavior not verified in the built binary or source.

## Success criteria

1. Zero dangling links in `ARCHITECTURE.md`.
2. Sections 3.5-3.7 exist and each links its normative document.
3. Source layout matches `git ls-files` for the listed directories.

## Out of scope / risks

- Recreating the five missing ADRs (separate decision). Risk: other docs (CLAUDE.md line 7 "ADR 0001-0005") repeat the wrong range; fixed in subtask 06.
