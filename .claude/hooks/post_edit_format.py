#!/usr/bin/env python3
"""PostToolUse / Write|Edit|MultiEdit.

After the agent writes a file we (a) auto-format C/C++ sources with
``clang-format -i`` using the repository ``.clang-format`` (Chromium-based, the
project's canonical style), and (b) append the changed path to
.claude/logs/edits.log.

This is the C++ analogue of a Python ``ruff format`` post-edit hook: there is no
Black/Prettier/ruff config in this repo - style is governed entirely by
``.clang-format`` and checked by ``.clang-tidy``. Formatting failures never
block: the hook exits 0 with a note so a transient tooling issue cannot wedge
the session. (Static analysis via clang-tidy needs the build's
compile_commands.json and is intentionally left to the build/CI, not this fast
post-edit hook.)
"""
from __future__ import annotations

import subprocess
import sys

from _common import REPO_ROOT, allow, append_log, edited_path, read_event

# C / C++ translation units and headers clang-format should touch.
CXX_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl"}


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

    allow()


if __name__ == "__main__":
    main()
