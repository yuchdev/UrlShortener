#!/usr/bin/env python3
"""PreToolUse / Bash guard.

Blocks destructive or production-targeting shell commands before they execute.
Covers both POSIX shells and PowerShell (the canonical host is Windows), so the
same guard applies whichever the agent uses.

Override the production guard for a deliberate, audited action by exporting
``URLSHORTENER_PROD_CONFIRMED=1`` in the environment before launching the agent.

Every block is recorded to .claude/logs/bash-guard.log for the audit trail.
"""
from __future__ import annotations

import os
import re

from _common import allow, append_log, block, read_event, tool_input

# (pattern, human reason). Patterns are matched case-insensitively.
DESTRUCTIVE: list[tuple[str, str]] = [
    (r"\brm\s+-[a-z]*r[a-z]*f|\brm\s+-[a-z]*f[a-z]*r", "recursive force delete (rm -rf)"),
    (r"\bRemove-Item\b.*-Recurse\b.*-Force\b|\bRemove-Item\b.*-Force\b.*-Recurse\b",
     "recursive force delete (Remove-Item -Recurse -Force)"),
    (r"\brd\s+/s\b|\brmdir\s+/s\b", "recursive directory delete (rd /s)"),
    (r"\bDROP\s+TABLE\b", "DROP TABLE"),
    (r"\bDROP\s+DATABASE\b", "DROP DATABASE"),
    (r"\bTRUNCATE\s+TABLE\b", "TRUNCATE TABLE"),
    (r"\bgit\s+push\b.*--force\b|\bgit\s+push\b.*\s-f\b", "git push --force"),
    (r"\bgit\s+reset\s+--hard\b", "git reset --hard"),
    (r"\bgit\s+clean\s+-[a-z]*f", "git clean -f"),
    (r">\s*/dev/sd[a-z]", "raw disk overwrite"),
    (r"\bmkfs\b", "filesystem format (mkfs)"),
    (r"\bFormat-Volume\b|\bformat\s+[a-z]:", "volume format"),
    (r"\bvcpkg\s+remove\b.*--recurse", "recursive vcpkg remove"),
]

# Anything that looks like it touches a production target.
PROD = re.compile(r"\b(prod|production)\b", re.IGNORECASE)


def main() -> None:
    event = read_event()
    command = str(tool_input(event).get("command", ""))
    if not command.strip():
        allow()

    for pattern, reason in DESTRUCTIVE:
        if re.search(pattern, command, re.IGNORECASE):
            append_log("bash-guard.log", f"BLOCKED [{reason}] :: {command}")
            block(
                f"Blocked by url-shortener bash-guard: {reason}.\n"
                f"Command: {command}\n"
                "This operation is destructive and is never allowed automatically. "
                "If you are certain, ask the user to run it manually."
            )

    if PROD.search(command) and os.environ.get("URLSHORTENER_PROD_CONFIRMED") != "1":
        append_log("bash-guard.log", f"BLOCKED [prod-target] :: {command}")
        block(
            "Blocked by url-shortener bash-guard: command targets a production resource.\n"
            f"Command: {command}\n"
            "Set URLSHORTENER_PROD_CONFIRMED=1 in the environment to authorise production "
            "actions, or rephrase to target a non-production environment."
        )

    allow()


if __name__ == "__main__":
    main()
