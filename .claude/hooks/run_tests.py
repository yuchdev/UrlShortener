#!/usr/bin/env python3
"""Stop hook - gate the end of a session on format + build + tests.

Runs, in order, the same checks CI expects a clean session to satisfy, adapted
to the CMake/CTest/Boost.Test stack:

  1. ``clang-format -i`` on the C/C++ files changed this session - auto-fix the
     mechanically fixable style, mirroring ``ruff --fix``.
  2. ``cmake --build <build-dir> --config Debug`` - the change must compile.
  3. ``ctest --test-dir <build-dir> -C Debug -L unit --output-on-failure`` - the
     unit suite must be green.

If any step fails, the hook exits 2 so the agent is told to analyze that tool's
own output and fix what it flagged before ending the session - not to weaken the
check. To avoid infinite loops Claude Code suppresses this hook when the stop was
itself triggered by a prior stop-hook block (``stop_hook_active``).

Configuration via environment:
  * ``URLSHORTENER_BUILD_DIR``  - build directory to use (default:
    ``cmake-build``, then ``build``).
  * ``URLSHORTENER_SKIP_TESTS=1`` - skip the whole gate for a known-WIP session.

If the build directory or the cmake/ctest runners are unavailable, the gate
skips gracefully (exit 0) rather than blocking - configure a build dir to arm it.
"""
from __future__ import annotations

import os
import subprocess
from pathlib import Path
from typing import Optional

from _common import REPO_ROOT, allow, append_log, block, read_event

TIMEOUT_SECONDS = 900
CXX_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx", ".ipp", ".inl"}
BUILD_DIR_CANDIDATES = ("cmake-build", "build")


def _run(cmd: list[str]) -> Optional[subprocess.CompletedProcess[str]]:
    """Run ``cmd`` from the repo root, returning None if the runner is unavailable."""
    try:
        return subprocess.run(
            cmd, cwd=str(REPO_ROOT), capture_output=True, text=True,
            timeout=TIMEOUT_SECONDS, check=False,
        )
    except (FileNotFoundError, subprocess.TimeoutExpired):
        return None


def _tail(proc: subprocess.CompletedProcess[str], n: int = 40) -> str:
    return "\n".join((proc.stdout + proc.stderr).strip().splitlines()[-n:])


def _build_dir() -> Optional[Path]:
    override = os.environ.get("URLSHORTENER_BUILD_DIR")
    candidates = [override] if override else list(BUILD_DIR_CANDIDATES)
    for name in candidates:
        if not name:
            continue
        p = Path(name)
        if not p.is_absolute():
            p = REPO_ROOT / p
        if p.is_dir():
            return p
    return None


def _changed_cxx() -> list[str]:
    proc = _run(["git", "diff", "--name-only", "HEAD"])
    if proc is None or proc.returncode != 0:
        return []
    out = []
    for ln in proc.stdout.splitlines():
        ln = ln.strip()
        if ln and Path(ln).suffix.lower() in CXX_SUFFIXES:
            p = REPO_ROOT / ln
            if p.is_file():
                out.append(str(p))
    return out


def main() -> None:
    event = read_event()
    if event.get("stop_hook_active"):
        allow()
    if os.environ.get("URLSHORTENER_SKIP_TESTS") == "1":
        append_log("tests.log", "SKIPPED (URLSHORTENER_SKIP_TESTS=1)")
        allow()

    # Step 1: format changed C/C++ files (fix-in-place; never blocks on its own).
    changed = _changed_cxx()
    if changed:
        _run(["clang-format", "-i", "-style=file", *changed])

    build_dir = _build_dir()
    if build_dir is None:
        append_log("tests.log", "SKIPPED (no build directory; set URLSHORTENER_BUILD_DIR)")
        allow()

    steps = [
        ("build", ["cmake", "--build", str(build_dir), "--config", "Debug"]),
        ("ctest unit", ["ctest", "--test-dir", str(build_dir), "-C", "Debug",
                        "-L", "unit", "--output-on-failure"]),
    ]

    for label, cmd in steps:
        proc = _run(cmd)
        if proc is None:
            append_log("tests.log", f"SKIPPED (runner unavailable: {label})")
            allow()
        if proc.returncode != 0:
            append_log("tests.log", f"FAIL ({label})")
            block(
                f"Stop blocked by url-shortener run-tests: `{' '.join(cmd)}` failed.\n"
                f"{_tail(proc)}\n"
                f"Analyze the {label} output above and fix every error/warning it left behind "
                "before ending the session. If a fix isn't clearly safe, ask the user instead of "
                "guessing. Export URLSHORTENER_SKIP_TESTS=1 only if this is intentional WIP."
            )

    append_log("tests.log", "PASS")
    allow()


if __name__ == "__main__":
    main()
