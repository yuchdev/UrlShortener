#!/usr/bin/env python3
"""postToolUse (edit|create) + agentStop: documentation reference integrity
(Copilot CLI port).

Runs ``scripts/check_doc_links.py`` so a docs edit never silently leaves a
dangling relative link or missing heading anchor.

* **postToolUse** - when the edited file is Markdown, check that file's outbound
  links/anchors immediately (scoped, cheap).
* **agentStop** - if any Markdown changed in the working tree this session, run
  the full repo doc-link scan, which also catches *inbound* breakage (e.g. a
  renamed heading that other docs still link to).

Always **non-blocking**: it surfaces problems to the agent as additional context
but never wedges a session. If ``scripts/check_doc_links.py`` is not present yet,
the hook skips silently - see the migration/port report for the follow-up to
port that checker to this repo. Swap ``add_context`` for ``block_stop`` to make
it a hard gate once the checker exists.
"""
from __future__ import annotations

import subprocess
import sys

from _common import REPO_ROOT, add_context, allow, append_log, edited_path, read_event

CHECKER = REPO_ROOT / "scripts" / "check_doc_links.py"


def _run_checker(paths: list[str]) -> tuple[int, str]:
    if not CHECKER.is_file():
        return 0, ""
    try:
        proc = subprocess.run(
            [sys.executable, str(CHECKER), "--check", *paths],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=60,
            check=False,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired) as exc:
        sys.stderr.write(f"doc_link_check: checker unavailable ({exc}); skipped.\n")
        return 0, ""
    return proc.returncode, (proc.stdout + proc.stderr)


def _changed_markdown() -> list[str]:
    try:
        proc = subprocess.run(
            ["git", "diff", "--name-only", "HEAD"],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return []
    if proc.returncode != 0:
        return []
    return [ln for ln in proc.stdout.splitlines() if ln.strip().lower().endswith(".md")]


def main() -> None:
    event = read_event()
    target = edited_path(event)

    if target is not None:
        # postToolUse: only act on Markdown; scope to the edited file.
        if target.suffix.lower() != ".md" or not target.exists():
            allow()
        scan_paths = [str(target)]
    else:
        # agentStop: only when docs changed this session; then full scan.
        if not _changed_markdown():
            allow()
        scan_paths = []

    code, output = _run_checker(scan_paths)
    if code != 0 and output.strip():
        append_log("doc-link-check.log", "dangling documentation references found")
        add_context(
            "doc_link_check: dangling documentation references (non-blocking) -\n"
            + output.rstrip()
            + "\nRun `/link-check` (or `python scripts/check_doc_links.py`) to review."
        )
    allow()


if __name__ == "__main__":
    main()
