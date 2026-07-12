#!/usr/bin/env python3
"""Secret scanner - dual mode (Copilot CLI port).

1. As a preToolUse(edit|create) hook: scans the *content about to be written*.
   If a likely secret is detected it prints a ``deny`` decision to block the
   write.
2. As a CLI (``python secret_scan.py <file> [<file> ...]``): scans existing
   files on disk; used by the /secret-scan skill and the dependency-audit flow.

Detection is pattern based and deliberately conservative-but-loud: it favours
catching real credentials over silence. Findings are logged to
.github/hooks/logs/secret-scan.log. The service redacts secrets at startup/log
time via ``observability::redactSecretValue`` - this hook stops them entering the
repo at all.

Pattern set is tuned for this C++ URL shortener's real secret surface: TLS
private keys (``--tls-key`` PEM files), PostgreSQL/Redis connection strings with
inline passwords, the analytics HMAC salt, generic assigned secrets, and GitHub
tokens (the repo uses the ``gh`` CLI / GitHub MCP). AI-provider / cloud keys from
the reference project are intentionally omitted - this service integrates none.
"""
from __future__ import annotations

import re
import sys
from collections.abc import Iterable
from pathlib import Path

from _common import REPO_ROOT, allow, append_log, deny, edited_path, read_event, tool_input

# name -> compiled pattern. Patterns target high-signal credential shapes.
PATTERNS: dict[str, re.Pattern[str]] = {
    "Private key block": re.compile(
        r"-----BEGIN (?:RSA |EC |DSA |OPENSSH |PGP )?PRIVATE KEY-----"
    ),
    "Postgres URL with password": re.compile(r"postg(?:res|resql)://[^:\s]+:[^@\s]+@"),
    "Redis URL with password": re.compile(r"redis(?:s)?://(?:[^:\s]*:)?[^@\s]+@"),
    "GitHub token": re.compile(r"\bgh[pousr]_[A-Za-z0-9]{36,}\b"),
    "Analytics hash salt assignment": re.compile(
        r"(?i)(analytics[_-]?(?:client[_-]?)?hash[_-]?salt|url_shortener_analytics_hash_salt)"
        r"\s*[=:]\s*['\"][^'\"\s]{6,}['\"]"
    ),
    "Generic assigned secret": re.compile(
        r"(?i)(password|passwd|secret|token|api[_-]?key|passphrase)\s*[=:]\s*['\"][^'\"\s]{8,}['\"]"
    ),
}

# Substrings that mark an obvious placeholder, so we do not cry wolf.
ALLOWLIST = (
    "example",
    "placeholder",
    "your-",
    "your_",
    "changeme",
    "dummy",
    "xxxx",
    "${",
    "$env:",
    "<your",
    "redacted",
    "fake",
    "test",
    "dev-analytics-salt",  # the documented in-repo dev default, not a real secret
)

SKIP_SUFFIXES = {".lock", ".png", ".jpg", ".jpeg", ".gif", ".pdf", ".db", ".sqlite"}


def _is_placeholder(line: str) -> bool:
    low = line.lower()
    return any(token in low for token in ALLOWLIST)


def scan_text(text: str) -> list[tuple[str, int, str]]:
    """Return (finding_name, line_number, line_excerpt) tuples."""
    hits: list[tuple[str, int, str]] = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        if _is_placeholder(line):
            continue
        for name, pattern in PATTERNS.items():
            if pattern.search(line):
                hits.append((name, lineno, line.strip()[:120]))
    return hits


def _content_from_event(event: dict[str, object]) -> str:
    fields = tool_input(event)
    parts: list[str] = []
    # Copilot: create -> file_text ; edit -> new_str. Claude: content/new_string.
    for key in ("file_text", "content", "new_str", "new_string", "text"):
        val = fields.get(key)
        if isinstance(val, str):
            parts.append(val)
    edits = fields.get("edits")
    if isinstance(edits, list):
        for edit in edits:
            if isinstance(edit, dict):
                for k in ("new_str", "new_string"):
                    if isinstance(edit.get(k), str):
                        parts.append(edit[k])
    return "\n".join(parts)


def _hook_mode() -> None:
    event = read_event()
    target = edited_path(event)
    if target and target.suffix.lower() in SKIP_SUFFIXES:
        allow()
    text = _content_from_event(event)
    if not text:
        allow()
    hits = scan_text(text)
    if hits:
        where = target.name if target else "<pending write>"
        for name, lineno, excerpt in hits:
            append_log("secret-scan.log", f"BLOCKED {where}:{lineno} [{name}] {excerpt}")
        report = "\n".join(f"  - line {ln}: {name}" for name, ln, _ in hits)
        deny(
            f"Blocked by url-shortener secret-scan: possible secret in {where}:\n{report}\n"
            "Never commit credentials, TLS private keys, or DSNs with inline passwords. "
            "Use environment variables / ${VAR} references or a secrets manager, and load "
            "them through ServerConfig / YAML at runtime instead."
        )
    allow()


def _cli_mode(paths: Iterable[str]) -> None:
    total = 0
    for raw in paths:
        p = Path(raw)
        if not p.is_absolute():
            p = REPO_ROOT / p
        if not p.is_file() or p.suffix.lower() in SKIP_SUFFIXES:
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        for name, lineno, excerpt in scan_text(text):
            total += 1
            rel = p.relative_to(REPO_ROOT) if str(p).startswith(str(REPO_ROOT)) else p
            print(f"{rel}:{lineno}: {name}: {excerpt}")
    if total:
        print(f"\nsecret-scan: {total} potential secret(s) found.")
        sys.exit(1)
    print("secret-scan: clean.")
    sys.exit(0)


if __name__ == "__main__":
    if len(sys.argv) > 1:
        _cli_mode(sys.argv[1:])
    else:
        _hook_mode()
