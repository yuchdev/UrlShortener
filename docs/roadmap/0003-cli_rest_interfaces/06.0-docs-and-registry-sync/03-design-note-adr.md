# 03 - Design note / ADR

**Parent task:** 06.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** [Task 05.0](/docs/roadmap/0003-cli_rest_interfaces/05.0-tests/README.md)
**Blocks:** none

## Objective

Record the DTO-sharing architecture decision (Config DTO + command handler
+ two thin adapters, as described in `plan.md`'s Architecture section) as a
short design note or ADR, so it is discoverable independent of this
milestone's roadmap tree, and so the explicit non-goals are not lost once
the milestone is marked complete.

## Files to add

Use the `/adr-write` skill to scaffold, or add directly:

- `docs/adr/0006-cli-rest-shared-command-layer.md` (next available ADR
  number - confirm against `docs/adr/` contents at implementation time)

Content must restate, in ADR form:
- **Context:** REST-only today; two of the ten link-management commands'
  logic lived only inline in HTTP handlers before this milestone.
- **Decision:** one Config DTO + one `LinkCommandService` method per
  command, consumed by both a REST handler and a CLI adapter; neither
  adapter contains business logic.
- **Non-goals (explicit):** Observability/Compatibility/Redirects/Fallback
  routes get no CLI form (see `plan.md`'s exclusion table); the
  `LinkService`/`LegacyLinkStore` storage-path duality is not resolved by
  this milestone.
- **Consequences:** future commands (if the REST surface grows) follow the
  same three-piece pattern; the CLI's usefulness for multi-step scripting
  depends on the storage-scoping finding from Task 03.0 subtask 02.

## Tests

None (documentation-only).

## Constraints

- Cross-link from `docs/roadmap/0003-cli_rest_interfaces/plan.md` to the new
  ADR once it exists, and vice versa.

## Success criteria

- [ ] ADR exists, follows the repository's MADR template (per `/adr-write`),
      and captures context/decision/non-goals/consequences accurately.
- [ ] `plan.md` links to it.
