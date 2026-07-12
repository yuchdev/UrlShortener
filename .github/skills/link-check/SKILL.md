---
name: link-check
description: User-invoked as /link-check [path ...]. Validates that documentation relative links and heading anchors resolve across docs/ (plus repo-root Markdown files and .github/). Use after editing, splitting, merging, or renaming docs to catch dangling references.
user-invocable: true
---

# Link Check

Verify documentation cross-references resolve - every relative `[text](path)`,
`[text](path#anchor)`, and in-page `#anchor` points at an existing file and a real
heading.

> **Note:** the reference project shipped `scripts/check_doc_links.py`. That
> script does not exist in this repo yet, so this skill validates links directly
> with grep/glob/read (the steps below). The `.github/hooks/scripts/doc_link_check.py`
> hook currently *skips* when that script is absent, so this manual skill is the
> real checker until the script is ported. Porting `scripts/check_doc_links.py` is
> a tracked follow-up in the migration report; once it exists, run it here instead.

## Steps

1. **Enumerate targets.** Use `$ARGUMENTS` if given, else all `*.md` under
   `docs/`, repo-root `*.md`, and `.github/**`. Find them with `glob`.
2. **Extract links.** For each file, grep for Markdown links:
   ```bash
   git grep -nE "\]\(([^)]+)\)" -- '*.md'
   ```
   Split each target into `path` and optional `#anchor`.
3. **Validate file targets.** For every relative `path`, resolve it against the
   linking file's directory (or repo root for `/`-prefixed) and confirm the file
   exists (`glob`/`Read`). Report each `missing file`.
4. **Validate anchors.** For every `#anchor`, read the target file's headings,
   derive each heading's GitHub-style slug per
   [references/anchor-slug-rules.md](references/anchor-slug-rules.md) (mind the
   double-hyphen case), and confirm the anchor matches one. Report each
   `missing anchor`.
5. **Fix.** For each finding, open the offending file at the reported line and
   correct the path or anchor, or repoint to the moved target.
6. If a heading was renamed/moved, also run the **doc-xref** skill to fix
   *inbound* links from other docs and from code Doxygen comments - not just this
   file's outbound links.
7. Re-run steps 2-4 until clean.

## Output

Report the findings fixed and confirm a clean result (no missing files or
anchors). Note any link left intentionally dangling (e.g. a planned-but-unwritten
doc) so reviewers know it is deliberate.

Complement: the **doc-xref** skill patches *inbound* references from other docs and
code comments - link-check covers outbound, doc-xref covers inbound. Run both after
a rename.
