# 02 - ADR 0001 amendment and ADR index

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 01
**Blocks:** 16
**Estimated size:** S
**Suggested agent:** docs-updater

## Objective

Record that ADR 0002 amends ADR 0001's adapter layer (ADR 0002 "Relationship to
ADR 0001"), keep `docs/adr/README.md` accurate, and verify that the storage-path
text Task 02.0 had to change is actually changed.

## Dependencies

- Subtask 01 (final ADR 0002 status).
- Task 02.0 subtask on persistence tests and ADR update (owner of the ADR 0001 Negative/Non-goals text about the `LinkService` vs `LinkCommandService` storage duality).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/adr/0001-cli-rest-shared-command-layer.md` | modify | header status and amended-by link |
| `docs/adr/README.md` | modify | inventory rows for 0001-0003; status values; add note on missing template |

## Classes and functions (exact edits)

**ADR 0001 header** (currently `> **Status:** Accepted`):

```text
> **Status:** Accepted (amended by [0002](/docs/adr/0002-unified-command-model.md))
```

Add after `Superseded by`: `> **Amended by:** [ADR 0002](/docs/adr/0002-unified-command-model.md) - adapter layer only (registry + JSON envelope); Config DTOs, one service method per command and shared serializers stand.` Use the link style already used inside ADR 0001 (check the file; relative links if it uses relative).

Verify (do not rewrite) that 02.0 changed the "Negative"/"Non-goals" bullets that said the `LinkService` vs `LinkCommandService` duality is left unresolved; if still present, stop and send back to 02.0.

**docs/adr/README.md:**

1. Inventory table rows: 0001 `Accepted (amended by 0002)`, 0002 and 0003 with the final status from subtask 01.
2. The `Template` section says `docs/adr/template.md` exists; it does not (decision-log Q21). Either create the file by copying ADR 0001's section structure (Status header block, Context, Decision, Alternatives Considered, Consequences, Links) - preferred, small - or reword the section to point at ADR 0001 as the template. Record the choice in the PR.
3. Add to `Naming conventions`: "`Accepted (amended by NNNN)` is allowed when a later ADR narrows an earlier one without superseding it."

## Implementation steps

1. Apply edits; 2. `/doc-xref docs/adr/0001-cli-rest-shared-command-layer.md` to catch other places that quote the ADR 0001 status; 3. run `/link-check docs/adr`.

## Commands

```bash
git grep -n "ADR 0001\|0001-cli-rest" -- docs README.md ARCHITECTURE.md CLAUDE.md
```

## Tests

None executable; `/link-check docs/adr` clean.

## Constraints

- ADR 0001's decision text is not edited beyond header lines and what 02.0 already changed.

## Success criteria

1. Header and index agree on status wording.
2. The duality text in ADR 0001 no longer claims the problem is open.
3. Template reference resolves to an existing file.

## Out of scope / risks

- The five ADR links in `ARCHITECTURE.md` (which point at files that never existed) are handled in subtask 05, not here.
