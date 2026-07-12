"""Shared helpers for URL Shortener GitHub Copilot CLI hooks.

Ported from the Claude Code fleet (.claude/hooks/_common.py) to Copilot CLI's
native command-hook protocol. All hooks are Python (not bash) because the
canonical host is Windows 10/11 and Python 3 is already a hard project
dependency (CMake invokes it during configure/build) - this guarantees the hooks
run identically on Windows, Linux, and macOS without a POSIX shell.

Copilot CLI command-hook protocol (differs from Claude Code):
  * Hook input arrives as a single JSON object on stdin. For tool events it is
    ``{sessionId, timestamp, cwd, toolCalls: [{id, toolName, toolArgsJson}]}``.
  * A decision is returned by printing a JSON object to **stdout** and exiting 0:
      - block a tool call (preToolUse):
          {"permissionDecision": "deny", "permissionDecisionReason": "..."}
      - block session end (agentStop):
          {"decision": "block", "reason": "..."}
      - inject extra context (sessionStart / any event):
          {"additionalContext": "..."}
  * Printing nothing and exiting 0 means "no opinion / allow".

This module also tolerates the Claude-style ``{tool_name, tool_input}`` shape so
the same scripts keep working if invoked under either harness.
"""
from __future__ import annotations

import json
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Optional

# scripts/ -> hooks/ -> .github/ -> <repo root>
REPO_ROOT = Path(__file__).resolve().parents[3]
LOG_DIR = REPO_ROOT / ".github" / "hooks" / "logs"


def read_event() -> dict[str, Any]:
    """Parse the hook event JSON from stdin, tolerating an empty stream."""
    raw = sys.stdin.read()
    if not raw.strip():
        return {}
    try:
        return json.loads(raw)
    except json.JSONDecodeError:
        return {}


def _first_tool_call(event: dict[str, Any]) -> dict[str, Any]:
    calls = event.get("toolCalls")
    if isinstance(calls, list) and calls and isinstance(calls[0], dict):
        return calls[0]
    return {}


def tool_name(event: dict[str, Any]) -> str:
    """Return the invoked tool name across Copilot/Claude event shapes."""
    call = _first_tool_call(event)
    if call.get("toolName"):
        return str(call["toolName"])
    return str(event.get("tool_name", ""))


def tool_input(event: dict[str, Any]) -> dict[str, Any]:
    """Return the tool arguments mapping across Copilot/Claude event shapes."""
    call = _first_tool_call(event)
    raw = call.get("toolArgsJson")
    if isinstance(raw, str) and raw.strip():
        try:
            parsed = json.loads(raw)
            if isinstance(parsed, dict):
                return parsed
        except json.JSONDecodeError:
            pass
    value = event.get("tool_input", {})
    return value if isinstance(value, dict) else {}


def edited_path(event: dict[str, Any]) -> Optional[Path]:
    """Resolve the file path targeted by an edit/create (Write/Edit) tool call."""
    fields = tool_input(event)
    raw = (
        fields.get("path")
        or fields.get("file_path")
        or fields.get("filePath")
        or fields.get("notebook_path")
    )
    if not raw:
        return None
    p = Path(str(raw))
    return p if p.is_absolute() else (REPO_ROOT / p)


def now_iso() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def append_log(filename: str, line: str) -> None:
    """Append a single timestamped line to a file under .github/hooks/logs/."""
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    with (LOG_DIR / filename).open("a", encoding="utf-8") as fh:
        fh.write(f"{now_iso()} {line}\n")


def _emit(obj: dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(obj))
    sys.stdout.flush()


def deny(reason: str) -> None:
    """Block a tool call (preToolUse). Terminal decision."""
    _emit({"permissionDecision": "deny", "permissionDecisionReason": reason.rstrip()})
    sys.exit(0)


def block_stop(reason: str) -> None:
    """Block session/agent stop (agentStop). Terminal decision."""
    _emit({"decision": "block", "reason": reason.rstrip()})
    sys.exit(0)


def add_context(text: str) -> None:
    """Inject additional context (sessionStart or any event) and exit."""
    _emit({"additionalContext": text.rstrip()})
    sys.exit(0)


def allow() -> None:
    """No opinion / allow: emit nothing and exit 0."""
    sys.exit(0)
