# 17 - Final link, registry and claims gates

**Parent task:** 08.0 Docs and registry sync
**State:** ⬜ Not started
**Depends on:** 05-13, 16
**Blocks:** none
**Estimated size:** S
**Suggested agent:** docs-updater (fixes), subtask-verifier (spec compliance), security-auditor (secret/leak read of docs)

## Objective

Close the documentation work with mechanical and factual gates: no dangling
links or anchors, a clean docs registry, no stale cross-references after
renames, no secrets in docs, and every user-facing claim spot-checked against
the built binary.

## Dependencies

- Skills: `/link-check [path ...]` (relative links and heading anchors under `docs/`, repo-root Markdown, `.claude/`; the `.claude/hooks/doc_link_check.py` hook checks similar defects on every edit), `/doc-registry` (registry of every `*.md` under `docs/` and `.claude/`, dangling references with rename candidates), `/doc-xref <target>` (inbound references to a path, anchor or symbol), `/secret-scan [paths]`, `/verify-subtask <path>`.
- A built Release/Debug binary with TUI enabled.

## Files affected

| Path | create/modify | What changes |
|---|---|---|
| any file with a finding | modify | link/anchor/reference fixes only |
| `docs/roadmap/0006-unified-command-model-and-tui/status.md` | modify | record gate evidence |

## Classes and functions (procedure)

1. **/link-check** on: `docs`, `README.md`, `ARCHITECTURE.md`, `CLAUDE.md`, `.claude`. Expect zero findings; fix each finding at the referrer.
2. **/doc-registry**: zero dangling references; every new file registered (subtask 13). Resolve rename candidates (the `ARCHITECTURE.md` dangling ADR links are a known case, fixed in 05).
3. **/doc-xref** for each moved or renamed target in this milestone: `docs/roadmap/0006-unified-command-model-and-tui/07.0-tests-and-conformance/` (old subtask files deleted: `01-conformance-corpus-and-adapter-shims.md`, `02-tui-snapshot-and-mock-client-tests.md`, `03-security-and-regression-tests.md`), `.../08.0-docs-and-registry-sync/` (old `01-adr-status-and-cross-references.md`, `02-tui-guide-and-cli-api-docs.md`, `03-reconcile-0002-and-0004-plans.md`). Update every inbound reference (typically task READMEs, status.md, plan.md, decision-log).
4. **/secret-scan** on `docs`, `README.md`, `CLAUDE.md`, `tests/conformance`, `tests/unit/tui/golden`.
5. **Claims spot check** (record command + result in the PR; 20 claims minimum):
   - 5 CLI flags from `docs/cli/README.md` against `--help`;
   - the `curl` examples in `README.md`/catalog against a local server (`/api/v1/commands`, `/api/v1/links`, list);
   - 3 TUI hotkeys against the live TUI (manual) and against goldens;
   - exit codes: run one failing command per code `1..5` and compare with the CLI table;
   - ARCHITECTURE source layout vs `git ls-files`;
   - every `ctest` command in `CLAUDE.md`/`README.md`/`docs/testing/README.md` runs (use `-N` to list matches);
   - persistence claim: create via `cmd`, restart, read via `cmd` (SQLite).
6. **/verify-subtask** for each 08.0 subtask file (17 invocations; batch). PARTIAL/FAIL items are fixed.
7. Record results in `status.md` (task 08.0 row complete, evidence links).

## Implementation steps

Follow the procedure; each failing check returns to the owning subtask rather than being waived.

## Commands

```bash
# inside Claude Code: /link-check   /doc-registry   /doc-xref docs/roadmap/0006-unified-command-model-and-tui/07.0-tests-and-conformance/01-conformance-corpus-and-adapter-shims.md   /secret-scan docs README.md CLAUDE.md
git grep -n "TODO\|TBD\|UNVERIFIED\|FIXME" -- docs/commands docs/tui docs/cli README.md ARCHITECTURE.md CLAUDE.md
cmake-build/url_shortener cmd command.list --raw
```

## Tests

`ctest --test-dir cmake-build -R "^docs__" --output-on-failure` (drift tests) plus the doc skills above.

## Constraints

- No gate is waived by editing the checker; fix the document.
- `UNVERIFIED` markers must not survive in published reference docs (they may remain in roadmap specs until their owning task is done).

## Success criteria

1. Link check and registry report zero findings.
2. `git grep` for stale subtask filenames returns nothing outside git history.
3. Secret scan clean; claims spot-check table committed in the PR with 20+ rows all matching.
4. `/verify-subtask` PASS for all 08.0 subtasks.

## Out of scope / risks

- Publishing docs to an external site. Risk: Windows CRLF causing the hook's anchor checks to differ from Linux CI; check line endings of edited files (`git diff --stat` warnings).
