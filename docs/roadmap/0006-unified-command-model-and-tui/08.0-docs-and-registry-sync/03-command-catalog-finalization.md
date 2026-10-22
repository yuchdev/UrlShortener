# 03 - Command catalog finalization

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** Task 01.0 (generator), Task 07.0 subtask 16 (drift test green)
**Blocks:** 04, 05, 06, 07
**Estimated size:** S
**Suggested agent:** docs-updater

## Objective

Turn `docs/commands/README.md` from "hand-maintained design" into the
generated, normative catalog: correct its status banner, make every worked
example executable, point section 7 at the real conformance corpus, and remove
statements that were true only before the generator existed.

## Dependencies

- 01.0 generator and its begin/end markers; 07.0 subtask 16 (drift test) and 15 (coverage gate); 07.0 corpus README (`tests/conformance/commands/README.md`).

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/commands/README.md` | modify (hand-written regions only) | banner, section 2 examples, section 6/7 text |

## Classes and functions (exact edits)

0. **Generated/prose split (open decision D9, recorded in the decision log).** Task 01.0 subtask 12 generates only block `### 3.7`; hand-written sections 1-6 remain prose and are drift-tested by 07.0 subtask 16. The catalog banner already states this. Confirm the user's outcome of D9; if the maintainers prefer full generation, delete 3.1-3.6 prose and keep only examples.
1. **Status banner (lines 3-9):** when ADR 0002 is accepted, change `Status: Proposed` to `Status: Accepted` and fill the real generator command (`command_catalog_dump`) into the generation-split sentence; name both drift tests (`commands__12_catalog_drift` for 3.7, `docs__03_command_catalog_matches_registry` for the prose).
2. **Section 1 error table:** hand-written (with the legacy resource-route column added on the design branch) and pinned by `docs__03_command_catalog_matches_registry` against `all_error_infos()`; re-verify only.
3. **Section 2 worked example:** every command line in the "one command on every transport" block must exist as corpus case `link_create_minimal` (07.0 subtask 11) with the same args; replace `$BASE`/placeholders with the flag spellings that `cmd --help` really prints (`--json`, `--arg`, `--raw`, `--no-wait` verified, `--read-only`, `--dry-run`/`--async` if different).
4. **Section 3.3 `link.import` row:** align `path` rejection wording, `on_conflict` behavior, error cap and row-number base with 05.0 as built; add the export size cap value.
5. **Section 3.6:** keep "Deliberately not commands"; `db.migrate` was already removed from ADR 0002/0003 on the design branch (ADR 0003 rule 4 keeps only a reserved `MIGRATE` rule for future `migrate`-permission commands); verify nothing reintroduced it.
6. **Section 6:** replace the inline "New transport" paragraph with a one-paragraph summary plus link to `/docs/commands/adding-a-transport.md`.
7. **Section 7 Conformance:** replace prose with links: corpus README, `tests/conformance/commands/`, the six adapter shims (list by transport id), the drift and coverage gates, and the command `ctest -L conformance`.
8. **Catalog corrections already applied on the design branch (2026-10-19) - re-verify each against the built code, do not re-apply:**
   - error table: `reserved_slug` HTTP **409** (`statusForAppError`), CLI exit 2, separate `invalid_slug` / `reserved_slug` rows, legacy resource-route column (`invalid_request`, `repository`, `internal_error`), no `job_not_found` code.
   - `link.preview` reduced projection; `link.stats` two shapes, epoch-second `from`/`to`, `bucket` `hour|day|week`.
   - REST create accepts both `url` and `target_url` (`url` first); `result` names the target `url`.
   - `link.list` `status` enum includes `expired`; `active` excludes expired links.
   - CSV columns `full_url,slug`; export formula-injection note; read-only sessions cannot export to `path`; `serve-stdio --path-root` / `--no-file-paths`.
   - Section 2 "REST access control" (gate, role flag, loopback-only admin, `manage_users` refused).
   **Still open, apply after implementation:** result field facts not yet in the catalog (`LinkView` JSON has no `enabled`/`expires_at`/`deleted_at` keys; lifecycle is `status`; `stats{...}` block present); `link.import` result rows/errors (line base, cap values) and `overwrite` availability as built by 05.0 (open decision D2); the outcome of Q9 and of any `async_capable` commands actually shipped; the additive `sortable` column key proposed by 06.0/14.
9. Add a short `## 8. Versioning` paragraph if 01.0 defined policy beyond "additive changes do not bump `v`" (otherwise skip).

## Implementation steps

1. Run the generator; confirm no diff in generated regions (otherwise the drift test would already fail).
2. Apply hand edits; run each worked-example command against the built binary and compare outputs with the doc.
3. `/link-check docs/commands`.

## Commands

```bash
cmake --build cmake-build --target docs__03_command_catalog_matches_registry
ctest --test-dir cmake-build -R "^docs__0[345]_" --output-on-failure
cmake-build/url_shortener cmd --help
```

## Tests

`docs__03_command_catalog_matches_registry` and conformance gate `conformance_20_coverage_gate` stay green.

## Constraints

- Never edit between generator markers by hand.
- Examples contain only `example.com` URLs.

## Success criteria

1. No remaining text says "Proposed", "hand-maintained", or "until the generator exists".
2. Each example in section 2 reproduces on the built binary.
3. Drift test green after the edit.

## Out of scope / risks

- Risk: generator emits whole file (then there are no hand regions); in that case this subtask edits the generator's template (01.0 code) via the owning task instead.
