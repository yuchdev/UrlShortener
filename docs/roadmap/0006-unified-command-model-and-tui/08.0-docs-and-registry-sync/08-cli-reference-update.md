# 08 - CLI reference update

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** Tasks 02.0, 04.0, 05.0, 06.0 (shipped flags)
**Blocks:** 17
**Estimated size:** M
**Suggested agent:** docs-updater

## Objective

Extend `docs/cli/README.md` (currently: `## Storage limitation`, `## Invocation`,
`## Exit codes`, `## Commands`, `## Command pairs`, then one `###` per verb)
with the new subcommands, the shared exit-code table including the new codes,
and remove statements that Task 02.0 made false.

## Dependencies

- Subtask 03 (catalog final), 04.0 `cmd` flags, 05.0 `serve-stdio`/`--read-only`, 06.0 `tui` flags, 02.0 persistence.
- Ownership: Task 02.0's docs-sync subtask rewrites `## Storage limitation`, the "One real difference" paragraph below the pairs table, and adds a "Global options" subsection documenting `--storage-config` / `URL_SHORTENER_STORAGE_CONFIG`; Task 06.0 adds a small `tui` synopsis. This subtask **verifies** those edits and does the rest (below).
- Current facts in the file (to preserve): `link` exit codes 0-4 via `ExitCodeForAppError` in `src/cli/link_command_dispatch.cpp`; parse errors exit `1`; existing REST create example uses body key `url`.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| `docs/cli/README.md` | modify | sections below |

## Classes and functions (exact edits)

1. **Intro paragraph (lines 1-7):** say there are now four local modes: `link <verb>` (sugar), `cmd <command>` (generic), `serve-stdio`, `tui`, all over the same registry; link `docs/commands/README.md`.
2. **`## Storage limitation`, "Global options", "One real difference":** verify 02.0's rewrites (backend selection through `--storage-config`, in-memory remains per-process); if any still says "do not share state" unconditionally, return to 02.0.
3. **`## Invocation`:** add `### cmd`, `### serve-stdio` subsections and expand the `tui` synopsis added by 06.0 into a `### tui` subsection each with synopsis, flags table (name, meaning, default) copied from `--help` output, one example, output shape (full envelope, `--raw`, `--json -`, `--arg k=v`, `--no-wait`, `--read-only`, `--dry-run`/`--async` as shipped).
4. **`## Exit codes`:** add rows `5` (`not_authorized`) and the `busy`/`job_cancelled` -> `4` mapping, and a column "Error code(s)" so the table mirrors the catalog error table (`docs/commands/README.md` section 1); state explicitly the two behaviors that differ: `link` parse errors exit `1` (legacy, unchanged per contract C4) while `cmd` argument errors exit `2` (`invalid_field`).
5. **`## Commands`:** add a "`link` verb -> command name" column to the table (`link create` -> `link.create`, ...) and a paragraph: `link <verb>` flags compile to the same `args` as `cmd <name> --json`; output of `link` is the `result` only (legacy), `cmd` prints the envelope.
6. **`## Command pairs`:** add rows for the new commands (`link.list`, `db.tables`, `db.rows`, `system.info`, `link.import`, `link.export`, `job.get`, `job.list`, `job.cancel`) with REST (`POST /api/v1/commands` and resource form where one exists) and CLI (`cmd ...`) columns; keep the existing rows. Reconcile the create example's REST body key (`url` vs `target_url`) with the Q18 outcome - show both only if both are accepted.
7. **Cross-check** (no authoring): the pairs-table create row and every other row use the flag names that `--help` prints, and `--storage-config` appears in each new subcommand's flag table.
8. **Per-verb `###` sections:** leave unchanged except links to `docs/commands/README.md#linkcreate`-style anchors for the args table where a link adds value (do not duplicate arg docs).
9. **New `## Jobs from the CLI`:** `link.import`/`link.export` waiting behavior, `--no-wait`, why `--no-wait` is only useful against a long-lived process, `job.get` polling.
10. **New `## Security notes`:** CLI runs as the local operator, `--read-only` downgrade, `path` args allowed locally and rejected on REST, no listener, no secrets in output.

## Implementation steps

1. Capture `--help` of each subcommand into a scratch file and paste flags; 2. run each example once and paste real output; 3. check relative links (this file uses `../api/README.md` style); 4. `/link-check docs/cli`.

## Commands

```bash
cmake-build/url_shortener cmd --help
cmake-build/url_shortener serve-stdio --help
cmake-build/url_shortener tui --help
cmake-build/url_shortener cmd link.import --json '{"format":"csv","data":"https://example.com/a,abc"}'
```

## Tests

None executable. Spot-checked in subtask 17.

## Constraints

- No flag or exit code documented that is not in `--help`/source.
- Field-level documentation stays in `docs/api/README.md` and the catalog (link, don't copy).

## Success criteria

1. Every flag in the new sections appears in `--help` output verbatim.
2. Exit-code table equals the catalog table row for row, plus the documented `link` legacy difference.
3. No remaining sentence says separate CLI invocations never share state without the in-memory qualifier.

## Out of scope / risks

- Man pages, shell completions. Risk: flag spellings changing late in 04.0/05.0; this subtask runs last.
