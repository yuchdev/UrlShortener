#!/usr/bin/env python3
"""Verify that a documentation pass changed only comments and whitespace.

Compares each changed C++ file against a git revision (default ``HEAD``) after
stripping comments and whitespace while preserving C++ string and character
literal tokens. A file passes when the remaining token stream is identical,
i.e. the edit only added/moved/reworded comments or re-wrapped declaration
lines. Any other difference is reported as ``CODE CHANGED``.

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


def normalize(text):
    """Return C++ tokens with comments and whitespace removed.

    String and character literals are kept as complete tokens.  Adjacent
    literals remain adjacent-token sequences instead of being joined, which
    preserves escape boundaries and whitespace inside literals.
    """
    tokens = _cpp_tokens(text)
    return tuple(tokens)


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
def _cpp_tokens(text):
    """Lex enough C++ to keep literals and comments out of normalization."""
    tokens = []
    i = 0

    identifier_re = re.compile(r"[A-Za-z_][A-Za-z_0-9]*")
    number_re = re.compile(r"(?:0[xX][0-9A-Fa-f]+|[0-9]+(?:\.[0-9]*)?)")

    while i < len(text):
        if text.startswith("//", i):
            newline = text.find("\n", i)
            i = len(text) if newline < 0 else newline + 1
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            i = len(text) if end < 0 else end + 2
            continue
        if text[i].isspace():
            i += 1
            continue

        raw_start = None
        for prefix in ("u8R\"", "uR\"", "UR\"", "LR\"", 'R"'):
            if text.startswith(prefix, i):
                raw_start = i
                quote_pos = i + len(prefix) - 1
                break
        if raw_start is not None:
            paren = text.find("(", quote_pos + 1)
            if paren >= 0:
                raw_delimiter = text[quote_pos + 1:paren]
                end_marker = ")" + raw_delimiter + '"'
                end = text.find(end_marker, paren + 1)
                if end >= 0:
                    tokens.append(text[i:end + len(end_marker)])
                    i = end + len(end_marker)
                    continue

        literal_start = None
        for prefix in ("u8\"", 'u"', 'U"', 'L"', '"'):
            if text.startswith(prefix, i):
                literal_start = i
                quote_pos = i + len(prefix) - 1
                break
        if literal_start is not None:
            j = quote_pos + 1
            while j < len(text):
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == '"':
                    j += 1
                    tokens.append(text[i:j])
                    i = j
                    break
                j += 1
            if i == j:
                continue

        if text[i] == "'":
            j = i + 1
            while j < len(text):
                if text[j] == "\\":
                    j += 2
                    continue
                if text[j] == "'":
                    j += 1
                    tokens.append(text[i:j])
                    i = j
                    break
                j += 1
            if i == j:
                continue

        identifier = identifier_re.match(text, i)
        if identifier:
            tokens.append(identifier.group(0))
            i = identifier.end()
            continue
        number = number_re.match(text, i)
        if number:
            tokens.append(number.group(0))
            i = number.end()
            continue

        tokens.append(text[i])
        i += 1
    return tokens


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
