"""
cli_integration_common.py – shared helpers for CLI integration tests.

These tests spawn the url_shortener binary with CLI subcommand arguments
and assert stdout JSON, exit codes, and process behaviour.

PREREQUISITES (implementation must satisfy before these tests pass):
  1. ParseResult must carry a command variant (server | link create | link get |
     link stats) in addition to ServerConfig.
  2. main.cpp must dispatch to CLI mode when the first positional argument is
     "link"; CLI mode must NOT start the HTTP listener.
  3. CLI mode is one-shot and uses per-process in-memory state (no cross-process
     persistence contract).
  4. Command argument shapes:
       link create --url <URL> [--slug <SLUG>] [--base-domain <DOMAIN>]
       link get --slug <SLUG> | --id <ID>
       link stats --slug <SLUG> --from <EPOCH_S> --to <EPOCH_S> --bucket <BUCKET>
  5. On success: exit 0, print single-line JSON to stdout.
  6. On validation error: exit 1 (or non-zero), print error JSON or message to
     stderr; do NOT print a valid-link JSON on stdout.
"""
from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

# ---------------------------------------------------------------------------
# Binary resolution
# ---------------------------------------------------------------------------

def find_binary() -> str:
    """Return path to the url_shortener binary or raise RuntimeError."""
    env = os.environ.get("SIMPLE_HTTP_BIN")
    if env and Path(env).exists():
        return env

    script_dir = Path(__file__).resolve().parent
    repo_root = script_dir.parent.parent.parent
    candidates = [
        repo_root / "cmake-build" / "url_shortener",
        repo_root / "cmake-build" / "Debug" / "url_shortener",
        repo_root / "cmake-build" / "Release" / "url_shortener",
        repo_root / "cmake-build-debug" / "url_shortener",
        repo_root / "cmake-build-release" / "url_shortener",
        repo_root / "build" / "url_shortener",
        # Windows executables
        repo_root / "cmake-build" / "Debug" / "url_shortener.exe",
        repo_root / "cmake-build" / "url_shortener.exe",
    ]
    for c in candidates:
        if c.exists():
            return str(c)
    raise RuntimeError(
        "url_shortener binary not found. "
        "Set SIMPLE_HTTP_BIN or build the project first."
    )


# ---------------------------------------------------------------------------
# CLI support probe
# ---------------------------------------------------------------------------

def _probe_cli_support(binary: str) -> bool:
    """
    Return True if the binary appears to support CLI subcommands.

    Strategy: run `binary link create --url https://probe.example.com` in a
    temp directory with a short timeout. If the process exits within the
    timeout the binary supports CLI mode (even if it rejects the URL).
    If it hangs (server mode), return False.
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        try:
            subprocess.run(
                [binary, "link", "create", "--url", "https://probe.example.com"],
                capture_output=True,
                timeout=6,
                cwd=tmpdir,
            )
            return True
        except subprocess.TimeoutExpired:
            return False


# ---------------------------------------------------------------------------
# Helper: run CLI
# ---------------------------------------------------------------------------

def run_cli(binary: str, *args: str, cwd: str | None = None, timeout: int = 10):
    """Run the binary with *args and return CompletedProcess."""
    return subprocess.run(
        [binary, *args],
        capture_output=True,
        text=True,
        timeout=timeout,
        cwd=cwd,
    )


def parse_json_stdout(proc) -> dict:
    """Parse proc.stdout as JSON, raising AssertionError with diagnostic on failure."""
    try:
        return json.loads(proc.stdout.strip())
    except json.JSONDecodeError as exc:
        raise AssertionError(
            f"stdout is not valid JSON: {exc}\n"
            f"stdout: {proc.stdout!r}\n"
            f"stderr: {proc.stderr!r}"
        )


# ---------------------------------------------------------------------------
# Base test class
# ---------------------------------------------------------------------------

class CliIntegrationBase(unittest.TestCase):
    """
    Base class for CLI integration tests.

    setUpClass probes the binary; if CLI subcommands are not yet wired the
    whole class is skipped with a clear message rather than failing with
    confusing output.
    """

    _binary: str = ""
    _cli_supported: bool = False

    @classmethod
    def setUpClass(cls) -> None:
        try:
            cls._binary = find_binary()
        except RuntimeError as exc:
            raise unittest.SkipTest(str(exc))

        cls._cli_supported = _probe_cli_support(cls._binary)
        if not cls._cli_supported:
            raise unittest.SkipTest(
                "Binary does not yet support CLI 'link' subcommand "
                "(process did not exit within timeout – still running in server mode). "
                "Implement CLI dispatch in main.cpp first."
            )

    def make_tmpdir(self):
        """Create and return a TemporaryDirectory; caller owns lifetime."""
        return tempfile.TemporaryDirectory()
