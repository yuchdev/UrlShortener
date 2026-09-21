# Heading → anchor slug rules

Backs the "update the anchor to the current GitHub-style heading slug" step of the
link-check skill. Anchors in this repo's Markdown are GitHub-flavoured slugs of the
heading text; derive them by this algorithm when fixing an anchor by hand or
validating one.

## The slug algorithm (GitHub-flavoured)

For a heading's text (after stripping the leading `#`s and surrounding spaces):

1. `strip()` the ends, then convert to **lowercase**.
2. Delete every character that is **not** a word char, whitespace, or hyphen
   (equivalent to `re.sub(r"[^\w\s-]", "")`) — punctuation
   (`.,:;!?()[]{}"'/\` etc.), `&`, em dashes `—`, arrows `→`, and emoji are
   **removed**, not replaced.
3. Replace **each whitespace character** with a hyphen (`re.sub(r"\s", "-")`).
   Note: *each* space, not each run — two spaces become two hyphens.

Underscores `_` and existing hyphens survive. There is **no** run-collapsing and
**no** trailing-hyphen trimming — a heading ending in removed punctuation after a
space (e.g. `## Foo :`) yields a trailing hyphen (`#foo-`).

## Worked examples

| Heading | Slug (`#…`) |
|---------|-------------|
| `## Redirect resolution` | `#redirect-resolution` |
| `## Link lifecycle (active/expired)` | `#link-lifecycle-activeexpired` |
| `### Cache-aside & invalidation` | `#cache-aside--invalidation` |
| `## P0 — must test` | `#p0--must-test` |
| `## StorageFactory backends` | `#storagefactory-backends` |
| `## Step ordering: resolve → redirect` | `#step-ordering-resolve--redirect` |

Note the **double hyphen**: `&`, `—`, `:`, and `→` are removed, but the spaces
that surrounded them each become a hyphen, so `A & B` → `a--b`. This is the most
common hand-fix mistake — don't collapse it to a single hyphen.

## Duplicate headings

If two headings produce the same slug, GitHub appends `-1`, `-2`, … to the second
and later occurrences (in document order): `#foo`, `#foo-1`, `#foo-2`.

## Fixing procedure

1. Find the *current* heading text in the target file.
2. Derive the slug with the rules above.
3. Update the link's `#anchor` to match.
4. If the heading itself was renamed/moved, also run the doc-xref skill to fix
   **inbound** links from other docs and from `src/` Doxygen comments — link-check
   only fixes the outbound anchors in the file you edited.
