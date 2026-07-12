#!/usr/bin/env python3
"""sessionStart hook - seed the session with live repo context (Copilot CLI port).

Emits, as additional context for the agent:
  * current git branch
  * the last 5 commits (one line each)
  * any open P0/P1 issues (via the GitHub CLI if authenticated; silent if not)

Copilot CLI sessionStart context is provided by printing a JSON object with an
``additionalContext`` field to stdout (see _common.add_context); the harness
injects that text into the conversation.
"""
from __future__ import annotations

import subprocess

from _common import REPO_ROOT, add_context


def _git(args: list[str]) -> str:
    try:
        out = subprocess.run(
            ["git", *args],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
        return out.stdout.strip()
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return ""


def _open_priority_issues() -> str:
    """Best-effort P0/P1 lookup via gh. Returns empty string when unavailable."""
    try:
        out = subprocess.run(
            [
                "gh", "issue", "list",
                "--state", "open",
                "--label", "P0,P1",
                "--limit", "20",
                "--json", "number,title,labels",
                "--template",
                "{{range .}}  #{{.number}} {{.title}}\n{{end}}",
            ],
            cwd=str(REPO_ROOT),
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
        return out.stdout.strip() if out.returncode == 0 else ""
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return ""


def main() -> None:
    branch = _git(["rev-parse", "--abbrev-ref", "HEAD"]) or "(unknown)"
    log = _git(["log", "-5", "--pretty=format:  %h %s"]) or "  (no commits)"
    issues = _open_priority_issues()

    lines = [
        "# URL Shortener - session context",
        f"Branch: {branch}",
        "",
        "Recent commits:",
        log,
    ]
    if issues:
        lines += ["", "Open P0/P1 issues:", issues]
    else:
        lines += ["", "Open P0/P1 issues: none found (or gh not authenticated)."]
    lines += [
        "",
        "Reminders: the redirect path is a protected fast path - keep it narrow. "
        "Treat user-supplied URLs/slugs/bodies as untrusted (SSRF via private "
        "targets, oversized bodies). Never log secrets, TLS private keys, DSNs, "
        "tokens, or the analytics salt; use parameterized SQL only.",
        "Delegate per .github/agents/: app-architect (design/ADRs), cpp-expert "
        "(implementation), testing-expert / test-documenter (tests), "
        "feature-reviewer + security-auditor (review), docs-writer / docs-updater "
        "(docs), subtask-verifier (spec compliance), agent-orchestrator (coordination).",
    ]
    add_context("\n".join(lines))


if __name__ == "__main__":
    main()
