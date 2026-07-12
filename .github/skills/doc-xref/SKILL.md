---
name: doc-xref
description: User-invoked as /doc-xref <target>. Finds every inbound reference to a doc path, heading/anchor, or code symbol - across docs/ AND src/ Doxygen comments - so a rename, move, split, or reworded section propagates everywhere. Use before renaming/moving/splitting docs or public symbols.
user-invocable: true
---

# Doc Cross-Reference

Find and update everything that points **at** `$ARGUMENTS` (a doc path, a heading
text/anchor, or a public C++ symbol). Complements the link-check skill, which
validates a file's *outbound* links; this finds *inbound* references so a change
propagates instead of leaving stale pointers.

> **Note:** the reference project ran a `scripts/linkify_doc_mentions.py` pre-pass.
> It does not exist here; this skill searches directly with `git grep`. Porting
> that script is a tracked follow-up in the migration report.

## Steps

1. **Enumerate inbound references** across both trees:
   - **Docs**: links `](<path-or-anchor>)`, bare path mentions, and prose mentions
     of the heading/title in `docs/**`, repo-root `*.md`, and `.github/**`.
   - **Code**: Doxygen / comment pointers in `src/**`, `include/**`, and
     `tests/**` (e.g. `/// See docs/specs/core/link-lifecycle.md §3`), plus the
     symbol name itself when the target is a symbol.
   ```bash
   git grep -nF "<target>"   # exact path / anchor / symbol; repeat for old name + anchor
   ```
2. **Rename/move**: update every hit to the new path/anchor; `git mv` when renaming
   a whole file so history follows.
3. **Reworded section**: update mentions whose surrounding text now misstates the
   section (registry descriptions, "see X" summaries, titles).
4. Update the `docs/` index (`docs/README.md`) line if a file was added, renamed,
   or removed.
5. Verify with the **link-check** skill.

## Output

List each reference updated (`file:line`, old → new) and confirm the link-check
skill passes. Flag any reference you could not safely auto-update for human review.

Complement: the link-check skill validates *outbound* links from any file you
edited - doc-xref covers inbound, link-check covers outbound. Run both after a
rename.

## Completion checklist

- [ ] Every hit from `git grep` is updated - file:line list provided in output
- [ ] Any reference not safely auto-updated is explicitly flagged for human review
- [ ] The `docs/` index line updated if the file was renamed or moved
- [ ] The link-check skill reports clean after all edits
