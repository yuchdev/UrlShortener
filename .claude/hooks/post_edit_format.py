#!/usr/bin/env python3
"""PostToolUse / Write|Edit|MultiEdit.

After the agent writes a file we (a) auto-format C/C++ sources with
``clang-format -i`` using the repository ``.clang-format`` (Chromium-based, the
project's canonical style), (b) insert the blank line clang-format cannot
enforce between a declaration and the next member's doc comment, (c) report
any same-line Doxygen comments back to the agent as non-blocking context, and
(d) append the changed path to .claude/logs/edits.log.

This is the C++ analogue of a Python ``ruff format`` post-edit hook: there is no
Black/Prettier/ruff config in this repo - style is governed entirely by
``.clang-format`` and checked by ``.clang-tidy``. Formatting failures never
block: the hook exits 0 with a note so a transient tooling issue cannot wedge
the session. (Static analysis via clang-tidy needs the build's
compile_commands.json and is intentionally left to the build/CI, not this fast
post-edit hook.)
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

from _common import REPO_ROOT, allow, append_log, edited_path, read_event

# C / C++ translation units and headers clang-format should touch.
CXX_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl"}

# A line that opens a Doxygen comment: ///, ///<, //!, //!<, /**, /*!.
_DOC_OPENER_RE = re.compile(r"^\s*(///|//!|/\*\*|/\*!)")
# Code followed by a Doxygen comment on the same line (forbidden by the
# doxygen-documentation skill: comments go on the lines above a declaration).
_TRAILING_DOC_RE = re.compile(r"[;,{)]\s*(///<?|//!<?|/\*\*<|/\*!<)")


def _run_clang_format(path: str) -> None:
    try:
        subprocess.run(
            ["clang-format", "-i", "-style=file", path],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired) as exc:
        sys.stderr.write(f"post_edit_format: clang-format unavailable ({exc}); skipped.\n")


def _separate_documented_members(path: Path) -> None:
    """Insert one blank line between a declaration and the doc comment of
    the next declaration.

    clang-format cannot enforce this (``SeparateDefinitionBlocks`` covers only
    functions/classes), so the house rule from the doxygen-documentation
    skill - documented data members, constants and enumerators are separated
    by at least one empty line - is applied here. Whitespace-only: a blank
    line is inserted when a line that opens a Doxygen comment directly
    follows a code line ending in ``;`` or ``,``. Lines inside ``/* */``
    blocks are never treated as code.
    """
    try:
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
    except (OSError, UnicodeDecodeError):
        return

    out: list[str] = []
    in_block_comment = False
    prev_is_decl_end = False
    changed = False
    for line in lines:
        stripped = line.strip()
        if not in_block_comment and prev_is_decl_end and _DOC_OPENER_RE.match(line):
            out.append("\n")
            changed = True
        out.append(line)

        # Classify this line for the next iteration.
        if in_block_comment:
            prev_is_decl_end = False
            if "*/" in stripped:
                in_block_comment = False
            continue
        if stripped.startswith("/*") and "*/" not in stripped:
            in_block_comment = True
            prev_is_decl_end = False
            continue
        code = stripped.split("//", 1)[0].rstrip()
        prev_is_decl_end = (
            bool(code)
            and not stripped.startswith(("//", "/*", "*", "#"))
            and code.endswith((";", ","))
        )

    if changed:
        path.write_text("".join(out), encoding="utf-8")


def _trailing_doc_comments(path: Path) -> list[str]:
    """Return ``file:line`` locations of same-line Doxygen comments."""
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except (OSError, UnicodeDecodeError):
        return []
    try:
        shown = path.relative_to(REPO_ROOT)
    except ValueError:
        shown = path
    return [
        f"{shown}:{n}"
        for n, line in enumerate(lines, start=1)
        if not line.lstrip().startswith(("//", "/*", "*"))
        and _TRAILING_DOC_RE.search(line)
    ]


def _report_trailing_doc_comments(locations: list[str]) -> None:
    """Feed same-line doc comments back to the editing agent (non-blocking).

    Moving a trailing comment above its declaration and rejoining the
    declaration onto one line needs judgment, so it is reported rather than
    auto-fixed.
    """
    shown = locations[:20]
    more = len(locations) - len(shown)
    message = (
        "Same-line Doxygen comments found (house rule: comment on the lines "
        "above, declaration as the last line, on a single line if it fits in "
        "80 columns, one blank line between documented members - see "
        ".claude/skills/doxygen-documentation/SKILL.md 'Declaration line "
        "layout'). Move them: " + ", ".join(shown)
        + (f" (+{more} more)" if more > 0 else "")
    )
    print(json.dumps({
        "hookSpecificOutput": {
            "hookEventName": "PostToolUse",
            "additionalContext": message,
        }
    }))


def main() -> None:
    event = read_event()
    target = edited_path(event)
    if target is None:
        allow()

    rel = target
    try:
        rel = target.relative_to(REPO_ROOT)
    except ValueError:
        pass
    append_log("edits.log", f"edited {rel}")

    if target.suffix.lower() in CXX_SUFFIXES and target.exists():
        _run_clang_format(str(target))
        _separate_documented_members(target)
        trailing = _trailing_doc_comments(target)
        if trailing:
            _report_trailing_doc_comments(trailing)

    allow()


if __name__ == "__main__":
    main()
