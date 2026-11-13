#!/usr/bin/env python3
"""Verify that a documentation pass changed only comments and whitespace.

Compares each changed C++ file against a git revision (default ``HEAD``) after
stripping comments and all whitespace and joining adjacent string literals
(``"ab" "cd"`` == ``"abcd"``). A file passes when the remaining token stream is
identical, i.e. the edit only added/moved/reworded comments or re-wrapped
declaration lines. Any other difference is reported as ``CODE CHANGED``.

Usage:
    python3 check_comment_only_diff.py [--rev REV] [PATH ...]

PATH defaults to every modified ``*.hpp``/``*.h``/``*.cpp`` file in the
working tree. Exit status is 1 if any file changed beyond comments/whitespace.
"""

import argparse
import re
import subprocess
import sys

_SOURCE_SUFFIXES = (".hpp", ".h", ".cpp", ".cc", ".hh")

# Order matters: string/char literals are matched first so that comment
# markers inside them (e.g. "http://") are not treated as comments.
_TOKEN_RE = re.compile(
    r'"(?:\\.|[^"\\\n])*"'      # string literal
    r"|'(?:\\.|[^'\\\n])*'"     # char literal
    r"|/\*.*?\*/"               # block comment
    r"|//[^\n]*",               # line comment
    re.S,
)


def normalize(text):
    """Return ``text`` with comments and whitespace removed and adjacent
    string literals joined."""

    def keep_literals(match):
        token = match.group(0)
        return "" if token.startswith("/") else token

    without_comments = _TOKEN_RE.sub(keep_literals, text)
    compact = re.sub(r"\s+", "", without_comments)
    # Adjacent literals "ab""cd" -> "abcd" (only reachable after whitespace
    # removal, and a real empty literal is reduced identically on both sides).
    return compact.replace('""', "")


_SORTABLE_RE = re.compile(
    r"^\s*(#\s*include\b|using\s+[\w:]+\s*;)"
)


def _split_includes(text):
    """Return (sorted sortable lines, text without them).

    Sortable lines are the ones clang-format may reorder without changing
    meaning: `#include` (SortIncludes, also `# include` inside an indented
    `#if` block) and `using a::b;` declarations (SortUsingDeclarations).
    Whitespace inside each line is removed before comparison.
    """
    lines = text.splitlines()
    sortable = sorted(
        re.sub(r"\s+", "", l) for l in lines if _SORTABLE_RE.match(l)
    )
    rest = "\n".join(l for l in lines if not _SORTABLE_RE.match(l))
    return sortable, rest


def include_order_only(old, new):
    """True when the files differ only in the order of `#include` lines and
    `using` declarations (what `clang-format -i` with `SortIncludes` /
    `SortUsingDeclarations` produces). Same set of lines, everything else
    token-identical."""
    old_inc, old_rest = _split_includes(old)
    new_inc, new_rest = _split_includes(new)
    return old_inc == new_inc and normalize(old_rest) == normalize(new_rest)


def changed_files(rev):
    out = subprocess.check_output(
        ["git", "diff", "--name-only", rev, "--"], text=True
    )
    return [f for f in out.split() if f.endswith(_SOURCE_SUFFIXES)]


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--rev", default="HEAD")
    parser.add_argument("paths", nargs="*")
    args = parser.parse_args()

    paths = args.paths or changed_files(args.rev)
    failed = []
    reordered = []
    for path in paths:
        try:
            old = subprocess.check_output(
                ["git", "show", f"{args.rev}:{path}"], text=True,
                stderr=subprocess.DEVNULL,
            )
        except subprocess.CalledProcessError:
            continue  # new file: nothing to compare against
        with open(path, encoding="utf-8") as fh:
            new = fh.read()
        if normalize(old) == normalize(new):
            continue
        if include_order_only(old, new):
            reordered.append(path)
        else:
            failed.append(path)

    print(f"{len(paths)} file(s) checked against {args.rev}")
    for path in reordered:
        print(f"INCLUDE ORDER {path}  (clang-format include/using sorting; acceptable)")
    for path in failed:
        print(f"CODE CHANGED {path}")
    if not failed:
        print("OK: comments/whitespace only"
              + (" (plus include sorting)" if reordered else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
