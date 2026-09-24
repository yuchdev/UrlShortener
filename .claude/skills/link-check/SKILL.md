---
name: link-check
description: User-invoked as /link-check [path ...]. Validates that documentation relative links and heading anchors resolve across docs/ (plus repo-root Markdown files and .claude/). Use after editing, splitting, merging, or renaming docs to catch dangling references.
allowed-tools: Bash, Read, Grep, Glob, Edit
invocation: /link-check [path ...]
---

# Link Check

Verify documentation cross-references resolve - every relative `[text](path)`,
`[text](path#anchor)`, and in-page `#anchor` points at an existing file and a real
heading.

This is the **outbound** direction: does what a file points at resolve? For the
inbound direction - who points *at* a target you are about to rename - use
`/doc-xref <target>`. Run both after a rename.

> **Note:** the reference project shipped `scripts/check_doc_links.py`,
> `scripts/linkify_doc_mentions.py`, and `scripts/doc_registry.py`. None of those
> exist in this repo yet, so this skill validates links directly with
> grep/glob/read (the steps below). The `.claude/hooks/doc_link_check.py` hook runs
> on every edit and at session end and checks the same *kinds* of defect with a
> separate, faster inline implementation (it does **not** shell out to any script);
> the two deliberately agree on heading-slug rules. Porting the scripts is a
> tracked follow-up; once `scripts/check_doc_links.py` exists, run it here instead.

## Steps

1. **Enumerate targets.** Use `$ARGUMENTS` if given, else all `*.md` under
   `docs/`, repo-root `*.md`, and `.claude/**`. Find them with `glob`.
2. **Extract links.** For each file, grep for Markdown links:
   ```bash
   git grep -nE "\]\(([^)]+)\)" -- '*.md'
   ```
   Split each target into `path` and optional `#anchor`.
3. **Validate file targets.** For every relative `path`, resolve it against the
   linking file's directory (or repo root for `/`-prefixed) and confirm the file
   exists (`glob`/`Read`). Report each `dangling link`.
4. **Validate anchors.** For every `#anchor`, read the target file's headings,
   derive each heading's GitHub-style slug per
   [references/anchor-slug-rules.md](references/anchor-slug-rules.md) (mind the
   double-hyphen case), and confirm the anchor matches one. If the target section
   has a hand-written `<a id="...">` anchor, prefer that stable name over the
   generated slug. Report each `missing anchor`.
5. **Fix** each finding at the reported line:
   - **`missing anchor`** — update the anchor to the target's current heading slug.
   - **`dangling link`** — the target file does not exist. Fix the **link**, not
     the filename, when the correct file already exists elsewhere; `git grep` for
     the basename to locate a moved target.
6. If a heading was renamed/moved, also run `/doc-xref <target>` to fix *inbound*
   links from other docs and from code docstrings - not just this file's outbound
   links.
7. Re-run steps 2-4 until clean (no dangling links or missing anchors).

## Output

Report the findings fixed and confirm a clean result (no missing files or
anchors). Note any link left intentionally dangling (e.g. a planned-but-unwritten
doc) so reviewers know it is deliberate.

## Complement

| Tool                 | Direction | Checks                                                    |
|----------------------|-----------|-----------------------------------------------------------|
| `/link-check`        | outbound  | Link targets exist **and** anchors resolve (the gate)     |
| `/doc-xref <target>` | inbound   | References from other docs **and** `src/`/`tests/` comments |

## Completion checklist

- [ ] Every relative link target under the scoped paths resolves to an existing file
- [ ] Every `#anchor` resolves to a real heading slug (or hand-written `<a id>`)
- [ ] Every `dangling link` either repointed, or flagged as deliberate in the output
- [ ] `/doc-xref` run for any heading or file that was renamed or moved
