---
name: doc-registry
description: >
  User-invoked as /doc-registry. Builds a registry of every *.md file under
  docs/ and .claude/, then reports any cross-references that point at missing
  files, with rename candidates ranked by confidence. Use for a one-shot corpus
  health check before or after structural documentation changes (adding specs,
  reorganising folders, bulk renames).
allowed-tools: Bash, Read, Grep, Glob, Edit
invocation: /doc-registry
---

# Doc Registry

Build a corpus map of every `*.md` file under `docs/` and `.claude/`, then report
cross-references that point at files that do not exist.

> **Note:** the reference project backed this with `scripts/doc_registry.py` and a
> `scripts/linkify_doc_mentions.py` pre-pass. Neither exists here, so this skill
> builds the registry directly with grep/glob/git (steps below). Porting those two
> scripts is a tracked follow-up in the migration report.

## When to use

- Before restructuring `docs/` (get a baseline of which links will break).
- After adding a new spec or stage directory (confirm all internal links resolve).
- As a quick pre-commit check when bulk-renaming docs.
- To get the list of "orphan" links that link-check might not catch (missing files
  rather than missing anchors).

## Steps

1. **Build the file registry.** Enumerate the corpus:
   ```bash
   git ls-files '*.md' | sort
   ```
   Keep the set of existing `.md` paths.
2. **Extract every `.md` link target.**
   ```bash
   git grep -nE "\]\(([^)#]+\.md)(#[^)]*)?\)" -- '*.md'
   ```
   For each hit, resolve the target relative to the linking file's directory (or
   repo root for `/`-prefixed).
3. **Find broken references.** Any resolved target not in the registry is a miss.
   For each miss, rank rename candidates by matching the **basename** against the
   registry:
   - **[HIGH]** exactly one registry file shares the basename → almost certainly a
     moved file; update the link to the shortest correct relative path.
   - **[MEDIUM]** multiple basename matches → inspect and pick the right one; if
     ambiguous, flag for human review.
   - **[REVIEW]** no basename match → decide to create the file, remove the link,
     or repoint it to a related existing file.
4. Do **not** rename files - the correct file already exists at the candidate path;
   the link was written with a wrong path.
5. Re-run steps 2-3 until all `.md` cross-references resolve.
6. Run the **link-check** skill afterwards to validate anchors and non-`.md` links
   — doc-registry only checks file existence, not anchor correctness.

## Output

A human-readable report with:

- **Missing .md References**: each broken link with confidence tag, source
  location (`file:line`), and ranked rename candidates.
- **Summary**: registry size, total `.md` links scanned, missing count,
  high-confidence / needs-review split.

## Complement

| Tool | Checks |
|------|--------|
| doc-registry | Missing *.md files referenced by links |
| link-check | Broken anchors and non-.md file links |
| doc-xref `<target>` | Inbound references from other docs and code |

Run link-check after doc-registry for full coverage — they check orthogonal
things.
