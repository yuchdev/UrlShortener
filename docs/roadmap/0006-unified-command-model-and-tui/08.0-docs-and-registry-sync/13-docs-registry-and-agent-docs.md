# 13 - Docs registry and agent docs

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 04, 10, 11, 12
**Blocks:** 17
**Estimated size:** S
**Suggested agent:** docs-updater

## Objective

Register every new document in `docs/README.md` (the "Documentation Registry")
and update the agent-facing process docs that enumerate stages, CI jobs and
test requirements.

## Dependencies

- `docs/README.md` today has a 2-column table (`Path | Description`) with only `docs/api/README.md` and `docs/cli/README.md` rows (the registry is far from complete; this subtask adds the 0006 rows and does not attempt a full backfill beyond those).
- `/doc-registry` skill: builds a registry of every `*.md` under `docs/` and `.claude/` and reports dangling references with rename candidates.
- `docs/agent/ci-pipeline-specification.md` (sections 3-4: build, unit, integration, perf smoke jobs), `docs/agent/testing-requirements.md` (section 3.7 perf), `docs/agent/implementation-taskmap.md` (stage-based map: Stage 01-05 + section 7 cross-stage rule).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/README.md` | modify | add registry rows |
| `docs/agent/ci-pipeline-specification.md` | modify | document new/changed CI jobs |
| `docs/agent/testing-requirements.md` | modify | conformance and TUI requirements; perf registration note |
| `docs/agent/implementation-taskmap.md` | modify (optional) | add a short "Milestone 0006" pointer section **only if** the file is meant to cover post-Stage-05 work; otherwise skip and record why |

## Classes and functions (exact edits)

**`docs/README.md` rows to add** (Path | Description):

| Path | Description |
|---|---|
| `docs/commands/README.md` | Command catalog: JSON envelope, error codes, every command, route table; generated from the `CommandRegistry`. |
| `docs/commands/adding-a-transport.md` | How to add a transport (IPC example) over `ITransportAdapter`. |
| `docs/tui/README.md` | Terminal UI user guide. |
| `docs/tui/development.md` | Terminal UI contributor guide (rules, screens, testing). |
| `docs/adr/README.md` | Architecture decision records (0001-0003). |
| `docs/testing/README.md` | Project test plan; plus `docs/testing/tui_manual_smoke.md` and `docs/testing/testplans/conformance.md`. |
| `tests/conformance/commands/README.md` | Conformance corpus schema and authoring guide. |
| `tests/perf/README.md` | Perf guard usage and baseline procedure. |

Update the existing `docs/cli/README.md` row text: "all nine `link <verb>` subcommands" -> "`link <verb>` sugar, `cmd`, `serve-stdio`, `tui`, flags and exit codes".

**`ci-pipeline-specification.md`:** add subsection (after 3.3) `### 3.3.1 Conformance and TUI jobs` listing: unit matrix with `URLSHORTENER_ENABLE_TUI` ON/OFF, integration job includes `conformance`, CLI e2e step, perf guard in 4.1 (advisory, summary output); failure policy (hard gates: unit, conformance, drift, structural redirect guard; advisory: perf). Names must equal the workflow files edited in 07.0 subtask 33.

**`testing-requirements.md`:** in the section listing required test layers add: "command registry: descriptor/handler unit tests; every command has a corpus case (coverage gate); TUI: snapshot + property + state machine; security: allowlist, authz matrix, import limits" and fix 3.7 to reflect the `perf` label now being registered.

## Implementation steps

1. Edit the four files; 2. run `/doc-registry`; resolve every reported dangling reference or rename candidate relevant to 0006 files (fix the referrer, not the target, unless the target was renamed by this milestone); 3. run `/link-check docs/README.md docs/agent`.

## Commands

```bash
git ls-files "docs/**/*.md" | wc -l
```

## Tests

None executable; `/doc-registry` and `/link-check` clean for touched files.

## Constraints

- `docs/README.md` uses relative links (`api/README.md` style); keep.
- Do not claim CI jobs that do not exist in `.github/workflows/`.

## Success criteria

1. Every new Markdown file created by Milestone 0006 appears in the registry or is a roadmap file (roadmap docs are indexed by `docs/roadmap/README.md`).
2. CI spec job names equal the workflow job names.
3. `/doc-registry` reports no 0006-related finding.

## Out of scope / risks

- Full backfill of the registry for older docs (e.g. `docs/security/*`, `docs/storage/*`). Risk: the registry is incomplete by design; do not imply completeness.
