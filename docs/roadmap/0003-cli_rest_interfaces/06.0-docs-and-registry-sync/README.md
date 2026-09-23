# Task 06.0 - Docs and registry sync

**Parent milestone:** [Milestone 0003 - CLI & REST Command Interfaces](/docs/roadmap/0003-cli_rest_interfaces/plan.md)
**Status:** ⬜ Not started
**Depends on:** [05.0 Tests](/docs/roadmap/0003-cli_rest_interfaces/05.0-tests/README.md)

## Scope

Close out the milestone by correcting the stale CLI design assumptions
already in the repository and adding a durable reference for the new
surface, so the next reader does not have to reconstruct the architecture
from source.

## Subtasks

| # | Document | Status | Blocks |
|---|----------|--------|--------|
| 01 | [Correct CLI test docs](01-correct-cli-test-docs.md) | ✅ Complete | none |
| 02 | [CLI command reference](02-cli-command-reference.md) | ⬜ Not started | none |
| 03 | [Design note / ADR](03-design-note-adr.md) | ✅ Complete | none |

## Key constraints

- `docs/testing/cli_link_commands.md` and `docs/testing/testplans/cli.md`
  must no longer imply CLI mode touches `uri.txt` (per `plan.md`'s
  Background correction), and must cover all ten commands, not just
  create/get/stats.
- The new CLI command reference is cross-linked with
  [`docs/api/README.md`](/docs/api/README.md) (each CLI command names its
  corresponding `operation_id`) rather than duplicating field-by-field
  documentation that already lives there.
- The design note explicitly restates the non-goals from `plan.md`
  (Observability/Compatibility/Redirects/Fallback excluded; the
  `LinkService` vs. `LegacyLinkStore` storage-path duality left unresolved)
  so a future milestone doesn't rediscover them from scratch.
