#!/usr/bin/env python3
"""31_import_limits_process_level.py - process-level import/export limits.

Out-of-process proof (Milestone 0006, Task 07.0 subtask 20, threats
T-95/T-96/T-97/T-118) that the import/export path controls hold against the
**real** binary, not a mock:

  * REST (``POST /api/v1/commands``) can neither read nor write local files: a
    ``path`` on ``link.import`` / ``link.export`` returns ``unsupported`` and a
    sentinel file's unique content never appears in any response
    (``command_runtime.cpp`` leaves the REST runtime default-deny).
  * A request body over ``--max-request-body-bytes`` is rejected before parsing
    and the server stays alive.
  * ``serve-stdio`` survives an over-size request line and still answers the
    next one; ``--path-root`` confines ``path`` (an escaping path ->
    ``invalid_field``) and ``--no-file-paths`` makes ``path`` ``unsupported``.
  * The local ``cmd`` import of a nonexistent file fails cleanly (non-zero exit,
    no crash).
  * The ``tui`` ``--path-root`` / ``--no-file-paths`` flags parse and enforce
    mutual exclusion at the process level (the confinement wiring itself is unit
    tested in ``tests/unit/tui/23_tui_file_path_policy_test.cpp``; ``tui`` needs
    a TTY to run the UI, so only its flag parsing is reachable here).

Reuses ``conformance_common`` (``conformance_env`` / ``spawn`` /
``terminate_and_reap``) and ``conformance_rest`` (``ServerProcess`` /
``RestClient``). Standard library only; runs under ``python3 -I``.
"""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Dict, List, Optional, Tuple

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402
import conformance_rest as cr  # noqa: E402

#: A unique marker written into the sentinel file; it must never be echoed back
#: in any response (proof the file was never opened/read over REST).
_SENTINEL_MARKER = "SENTINEL-31-UNIQUE-DO-NOT-ECHO-a1b2c3"

#: Per-interaction wall clock for a stdio/cmd child.
_CHILD_TIMEOUT_S = 20.0


def _envelope(command: str, args: Dict[str, object],
              options: Optional[Dict[str, object]] = None) -> str:
    """Serialize one request envelope as a single NDJSON line (no newline)."""
    env: Dict[str, object] = {"v": 1, "command": command, "args": args}
    if options is not None:
        env["options"] = options
    return json.dumps(env, ensure_ascii=False)


def _run_stdio(binary: str, flags: List[str], lines: List[str],
               env: Dict[str, str]) -> List[dict]:
    """Run ``serve-stdio`` with *flags*, feed *lines*, return parsed responses.

    One response envelope is emitted per input line; EOF on stdin ends the
    session. The call is bounded by :data:`_CHILD_TIMEOUT_S`.
    """
    proc = cc.spawn(
        [binary, "serve-stdio", *flags],
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    try:
        stdin_text = "".join(line + "\n" for line in lines)
        out, _err = proc.communicate(input=stdin_text.encode("utf-8"),
                                     timeout=_CHILD_TIMEOUT_S)
    except subprocess.TimeoutExpired:
        cc.terminate_and_reap(proc)
        raise
    responses: List[dict] = []
    for raw in out.decode("utf-8", "replace").splitlines():
        raw = raw.strip()
        if not raw:
            continue
        try:
            responses.append(json.loads(raw))
        except json.JSONDecodeError:
            continue
    return responses


def _error_code(envelope: object) -> Optional[str]:
    """Return ``error.code`` of a response envelope, or ``None``."""
    if isinstance(envelope, dict) and envelope.get("ok") is False:
        error = envelope.get("error")
        if isinstance(error, dict):
            code = error.get("code")
            if isinstance(code, str):
                return code
    return None


class ImportLimitsProcessLevel(unittest.TestCase):
    """Process-level import/export size and path limit checks."""

    @classmethod
    def setUpClass(cls) -> None:
        cls.binary = cc.find_binary()
        cls.env = cc.conformance_env()
        cls._tmp = tempfile.TemporaryDirectory(prefix="conf31_")
        cls.tmp = Path(cls._tmp.name)
        cls.sentinel = cls.tmp / "sentinel.csv"
        cls.sentinel.write_text(
            "http://example.com/," + _SENTINEL_MARKER + "\n", encoding="utf-8"
        )
        # One admin REST server (loopback -> Admin); file paths stay default-deny.
        cls.ports: Dict[str, int] = {}
        cls.server = cr.ServerProcess(
            binary=cls.binary,
            storage_args=[],  # in-memory backend
            role="admin",
            log_path=cls.tmp / "server.log",
            env=cls.env,
        )
        cls.server.start()
        cls.ports["admin"] = cls.server.port
        cls.client = cr.RestClient(cls.ports)

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.stop()
        cls._tmp.cleanup()

    # ---- REST: no local file read/write; sentinel never echoed ----

    def _assert_rest_path_unsupported(self, command: str) -> None:
        body = _envelope(command, {"format": "csv",
                                   "path": str(self.sentinel)})
        status, parsed, raw, _ctype = self.client.post_command("admin", body)
        # `path` on REST is default-deny -> unsupported (HTTP 501 per catalog).
        self.assertEqual(_error_code(parsed), "unsupported",
                         f"{command}: expected unsupported, got {raw!r}")
        self.assertEqual(status, cc.HTTP_STATUS["unsupported"])
        # The sentinel's unique content must never appear in any response.
        self.assertNotIn(_SENTINEL_MARKER, raw,
                         f"{command}: sentinel content echoed in response")

    def test_rest_import_path_unsupported(self) -> None:
        self._assert_rest_path_unsupported("link.import")

    def test_rest_export_path_unsupported(self) -> None:
        self._assert_rest_path_unsupported("link.export")

    def test_rest_body_over_limit_rejected_before_parse(self) -> None:
        # A body well over the default --max-request-body-bytes (65536). The
        # oversize `data` is valid-looking JSON so only the size gate can reject.
        oversize = "x" * (200 * 1024)
        body = _envelope("link.import", {"format": "csv", "data": oversize})
        status, parsed, raw, _ctype = self.client.post_command("admin", body)
        # Rejected before/at parse: a transport 413, or the envelope/field code.
        code = _error_code(parsed)
        self.assertTrue(
            status == 413
            or code in ("invalid_envelope", "invalid_field"),
            f"oversize body not rejected: status={status} body={raw[:200]!r}",
        )
        # The server is still alive: a small command answers afterwards.
        ok_status, ok_parsed, ok_raw, _ = self.client.post_command(
            "admin", _envelope("command.list", {})
        )
        self.assertEqual(ok_status, 200, f"server not alive: {ok_raw[:200]!r}")
        self.assertIsInstance(ok_parsed, dict)
        self.assertTrue(ok_parsed.get("ok") is True)

    # ---- serve-stdio: oversize line survives; path confinement ----

    def test_stdio_oversize_line_survives(self) -> None:
        oversize_line = _envelope("link.import",
                                  {"format": "csv", "data": "y" * (200 * 1024)})
        good_line = _envelope("command.list", {})
        responses = _run_stdio(self.binary, [], [oversize_line, good_line],
                               self.env)
        # The oversize line is rejected but the session survives and answers the
        # following valid request (at least one ok response is produced).
        self.assertTrue(responses, "serve-stdio produced no responses")
        self.assertTrue(any(r.get("ok") is True for r in responses),
                        f"no surviving ok response: {responses!r}")

    def test_stdio_path_root_confines(self) -> None:
        root = self.tmp / "root"
        root.mkdir(exist_ok=True)
        # An absolute path OUTSIDE the confinement root must be refused
        # invalid_field (the file is never opened).
        escape = _envelope("link.import", {"format": "csv",
                                           "path": str(self.sentinel)})
        responses = _run_stdio(self.binary, ["--path-root", str(root)],
                               [escape], self.env)
        self.assertTrue(responses, "serve-stdio produced no response")
        self.assertEqual(_error_code(responses[0]), "invalid_field",
                         f"escape not confined: {responses[0]!r}")
        # No sentinel content leaks over stdio either.
        self.assertNotIn(_SENTINEL_MARKER, json.dumps(responses[0]))

    def test_stdio_no_file_paths(self) -> None:
        request = _envelope("link.import", {"format": "csv",
                                            "path": str(self.sentinel)})
        responses = _run_stdio(self.binary, ["--no-file-paths"], [request],
                               self.env)
        self.assertTrue(responses, "serve-stdio produced no response")
        self.assertEqual(_error_code(responses[0]), "unsupported",
                         f"path not disabled: {responses[0]!r}")

    # ---- cmd: nonexistent file fails cleanly ----

    def test_cmd_path_traversal_exit_code(self) -> None:
        missing = self.tmp / "does" / "not" / "exist.csv"
        proc = cc.spawn(
            [self.binary, "cmd", "link.import",
             "--arg", "format=csv",
             "--arg", f"path={missing}"],
            env=self.env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            out, _err = proc.communicate(timeout=_CHILD_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            cc.terminate_and_reap(proc)
            self.fail("cmd link.import hung on a nonexistent file")
        # A nonexistent import file fails cleanly: non-zero, no signal crash.
        self.assertGreater(proc.returncode, 0,
                           f"cmd should fail; rc={proc.returncode}")
        # And the unique sentinel content is nowhere in the output.
        self.assertNotIn(_SENTINEL_MARKER, out.decode("utf-8", "replace"))

    # ---- tui: flags parse / enforce mutual exclusion at the process level ----

    def _run_tui(self, flags: List[str]) -> Tuple[int, str]:
        proc = cc.spawn(
            [self.binary, "tui", *flags],
            env=self.env,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        try:
            out, err = proc.communicate(timeout=_CHILD_TIMEOUT_S)
        except subprocess.TimeoutExpired:
            cc.terminate_and_reap(proc)
            self.fail("tui invocation hung")
        return proc.returncode, (out + err).decode("utf-8", "replace")

    def test_tui_help_lists_path_flags(self) -> None:
        rc, text = self._run_tui(["--help"])
        self.assertEqual(rc, 0, f"tui --help rc={rc}")
        self.assertIn("--path-root", text)
        self.assertIn("--no-file-paths", text)

    def test_tui_path_flags_enforced(self) -> None:
        # A relative --path-root is a parse error (normalize_path_root rejects a
        # non-absolute value) -> non-zero exit, before the TTY check.
        rc_rel, _ = self._run_tui(["--path-root", "relative/dir"])
        self.assertGreater(rc_rel, 0, "relative --path-root should be rejected")
        # The two flags are mutually exclusive -> non-zero exit.
        rc_excl, _ = self._run_tui(
            ["--no-file-paths", "--path-root", str(self.tmp)]
        )
        self.assertGreater(rc_excl, 0,
                           "mutually exclusive tui flags should be rejected")


if __name__ == "__main__":
    unittest.main(verbosity=2)
