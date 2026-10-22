# 15 - Docs sync

**Parent task:** 02.0 Storage path unification
**State:** ⬜ Not started
**Depends on:** 02.0/13, 02.0/14
**Blocks:** none (closes Task 02.0)
**Estimated size:** S
**Suggested agent:** docs-updater

## Objective

Bring the existing documentation in line with the new behavior: the CLI can
share a persistent backend with the server via `--storage-config`; the storage
duality recorded as a non-goal in ADR 0001 is resolved; the process-local
limitation text is narrowed to the default in-memory mode. This is a
documentation-only subtask executed after the behavior is proven by 13 and 14.

## Dependencies

- Behavior and flag names from 02.0/10-14 (`--storage-config`,
  `URL_SHORTENER_STORAGE_CONFIG`, 5 s cross-process cache bound, 30 s counter
  write-behind, exit code 4 for startup storage errors).
- Existing text to change (verified by grep):
  - `docs/adr/0001-cli-rest-shared-command-layer.md` lines ~132-142: the
    non-goal "The `LinkService` vs. `LegacyLinkStore` storage-path duality is
    not resolved by this milestone ...".
  - `docs/cli/README.md` lines ~9-18 ("Storage limitation") and ~96-101 ("One
    real difference ...").
  - `docs/storage/configuration.md` (YAML examples; no mention of how the file is
    selected) and `docs/configuration.md` line ~55 (links to it).
- Tests' module docs mention the old statement: `cli_integration_common.py`
  prerequisite 3 ("per-process in-memory state (no cross-process persistence
  contract)") - updated in 13 or here if missed.

## Files affected

| Path | Create / modify | What changes |
|---|---|---|
| `docs/adr/0001-cli-rest-shared-command-layer.md` | modify | Replace the non-goal bullet with a "Resolved by Milestone 0006 Task 02.0" note and correct the description of the duality (see below) |
| `docs/cli/README.md` | modify | Rewrite "Storage limitation"; add `--storage-config` to a new "Global options" subsection; adjust the "One real difference" paragraph |
| `docs/storage/configuration.md` | modify | New section "Selecting the file": `--storage-config` flag, `URL_SHORTENER_STORAGE_CONFIG`, relative-path rule, file permission note, restart required |
| `docs/configuration.md` | modify | One row/sentence listing the flag and env var next to the existing storage pointer |
| `docs/storage/overview.md` | modify (if it describes which path the app uses) | UNVERIFIED whether it states the old duality; check and fix |
| `docs/roadmap/0006-unified-command-model-and-tui/status.md` | modify | Mark Task 02.0 progress (state markers only) |

## Classes and functions

None. Required content:

**ADR 0001** - the original bullet says the redirect path "reads through the
pluggable `IMetadataRepository` selected via `StorageConfig`/
`BuildStorageAdapters`". Reading the code shows `BuildStorageAdapters` and
`core::LinkService` were never wired into `main`/HTTP handlers; the redirect
path used the same legacy singletons as the CLI. Replace with: status note
"Storage-path duality resolved in Milestone 0006 Task 02.0: the redirect path,
REST and CLI obtain their `ILinkStore` from `app::LinkBackend`; with
`--storage-config` that is a `RepositoryLinkStore` over the configured
`IMetadataRepository`; the default remains the in-memory legacy store." Keep the
historical wording as a quoted "Original non-goal" so the decision history is
not rewritten.

**docs/cli/README.md** must state, in this order: default (no flag) = in-memory
per process, nothing persists between invocations (unchanged and still
asserted by `cli_03...` default cases); with `--storage-config <yaml>` (or
`URL_SHORTENER_STORAGE_CONFIG`) invocations share the configured backend and
see the server's data; example with SQLite YAML and two separate commands;
visibility bound between a running server and the CLI (cache TTL 5 s on
persistent backends); `link stats` still reads in-process click events and is
empty across processes (README R-3); counters from redirects are persisted at
most every 30 s and at server shutdown; the YAML may hold credentials, so keep
its permissions tight and never commit it.

**docs/storage/configuration.md** - examples stay; add the selection section and
the sentence that relative `sqlite_path` is resolved against the process working
directory (use absolute paths for CLI use).

## Implementation steps

1. Grep for stale statements:
   `Select-String -Path docs/**/*.md -Pattern "per-process|do not share state|non-persistent singleton|not resolved by this milestone"`.
2. Edit the files in the table.
3. Run the documentation checks (below).
4. Update `status.md`.

## Commands

```powershell
# find stale wording (PowerShell)
Get-ChildItem docs -Recurse -Filter *.md | Select-String -Pattern "per-process|do not share state|non-persistent singleton|not resolved by this milestone"
# link and registry checks (project skills)
#   /link-check docs
#   /doc-registry
#   /doc-xref docs/adr/0001-cli-rest-shared-command-layer.md
```

(`/link-check`, `/doc-registry`, `/doc-xref` are the repository's documentation
skills listed in the session; they validate relative links and cross-references.)

## Tests

No code tests. Checks:

| Check | How | Pass condition |
|---|---|---|
| Stale-wording grep | command above | Remaining hits are only the default-mode statements or historical quotes |
| Link check | `/link-check` | No dangling links in edited files |
| Example accuracy | Run the documented two-command example from `docs/cli/README.md` against a fresh binary | Second command prints the link created by the first (same procedure as the manual smoke in subtask 12) |
| Flag accuracy | Compare the documented flag/env names with `src/cli_parser.cpp` | Identical spelling |

## Constraints

- Docs must not claim persistence for the default mode (`cli_03` default cases
  still assert isolation).
- Do not print real credentials in examples; use placeholders and the
  `${VAR}`/environment convention (the repository's secret-scan hook blocks URLs
  with passwords).
- Absolute-from-repo-root links in roadmap documents; relative links as the
  surrounding file already uses elsewhere in `docs/`.
- ADR text changes preserve history (quote, do not erase).

## Success criteria

1. The stale-wording grep has no unexplained hits.
2. `/link-check` reports no new dangling references.
3. The documented example works verbatim.
4. ADR 0001 states that the duality is resolved and points to this task.

## Out of scope / risks

- ADR 0002/0003 wording: ADR 0002 "Negative consequences" and plan.md were
  already corrected on the design branch (the pluggable backend was never
  wired, not "dual"); Task 08.0 subtask 01 re-checks the wording when the ADRs
  are proposed for acceptance.
- Documenting `ExpiringLinkCache` / write-behind internals beyond user-visible
  behavior (belongs in the docs for operators of Task 08.0, if at all).
- Postgres/Redis operational guidance for the new wiring (needs real-service
  verification first).
