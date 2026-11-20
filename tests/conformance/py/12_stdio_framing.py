#!/usr/bin/env python3
"""12_stdio_framing.py - serve-stdio transport framing tests.

The behaviours here are properties of the NDJSON *wire*, not of any single
corpus case, so they cannot be expressed as corpus cases and live as explicit
``unittest`` tests instead: strict request/response ordering over a live pipe,
malformed-line recovery, blank-line suppression, the oversize-line cap, CRLF
termination, clean EOF shutdown, job progress between lines, and stdout/stderr
purity. The per-case framing already proven by the process-level integration
suite (``tests/integration/cli/40_serve_stdio_session.py`` /
``41_serve_stdio_jobs_and_eof.py``) is not duplicated; these are the conformance
transport's own framing contract (05.0 catalog section 2).

Type: integration (spawns the real ``url_shortener`` binary in serve-stdio mode;
no HTTP listener - contract C5).

Boundaries:
    - Ordering: 50 pipelined requests return in order with matching ids.
    - Recovery: a malformed line -> ``invalid_envelope`` (id ``srv-<n>``); the
      process answers the next valid line.
    - Suppression: a blank line yields no output line.
    - Caps: a line over ``--max-request-body-bytes`` -> ``invalid_envelope`` or
      ``invalid_field`` and the process survives.
    - CRLF: a ``\\r\\n``-terminated line is accepted.
    - EOF: closing stdin exits 0 with no trailing partial output.
    - Jobs: an import job is accepted, other commands answer while it runs, and
      ``job.get`` polling reaches a terminal state.
    - Purity: every stdout line is a v1 JSON envelope; logs go to stderr and the
      request URL never appears there (contract C6).

On failure first check:
    - Each process is spawned via ``conformance_common.spawn`` and reaped via
      ``terminate_and_reap`` (through ``StdioProc``); a leaked child means
      ``stop`` did not run - every test registers it with ``addCleanup``.
    - All reads are timeout-bounded, so a hung child fails by timeout, never a
      CTest hang.
"""

from __future__ import annotations

import json
import sys
import time
import unittest
from pathlib import Path

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))
_CLI_DIR = _THIS.parents[3] / "tests" / "integration" / "cli"
if str(_CLI_DIR) not in sys.path:
    sys.path.insert(0, str(_CLI_DIR))

import conformance_common as cc  # noqa: E402
from stdio_client import StdioProc  # noqa: E402

#: Terminal job states that stop ``job.get`` polling.
_TERMINAL = ("succeeded", "failed", "cancelled")


class StdioFramingTest(unittest.TestCase):
    """[Integration][Conformance] serve-stdio NDJSON wire framing contract."""

    @classmethod
    def setUpClass(cls) -> None:
        """Resolve the binary once (honours ``SIMPLE_HTTP_BIN``)."""
        cls._binary = cc.find_binary()

    def _proc(self, *args: str) -> StdioProc:
        """Spawn a serve-stdio child under a sanitised env, reaped on teardown."""
        proc = StdioProc(self._binary, "serve-stdio", *args,
                         env=cc.conformance_env())
        self.addCleanup(proc.stop)
        return proc

    def test_ordered_pipeline(self) -> None:
        # 50 requests written before any read return in order with matching ids;
        # repeated 3x (success criterion 2) to catch any reorder flake.
        commands = ("command.list", "link.get")
        for _ in range(3):
            proc = self._proc()
            for i in range(50):
                req = {"v": 1, "id": "c-%d" % i, "command": commands[i % 2]}
                if commands[i % 2] == "link.get":
                    req["args"] = {"slug": "absent"}
                proc.send_envelope(req)
            for i in range(50):
                resp = proc.read_json(timeout=10.0)
                self.assertEqual(resp["id"], "c-%d" % i, resp)
            proc.stop()

    def test_malformed_line_continues(self) -> None:
        proc = self._proc()
        proc.write_line("this is not json")
        bad = proc.read_json(timeout=10.0)
        self.assertFalse(bad["ok"], bad)
        self.assertEqual(bad["error"]["code"], "invalid_envelope", bad)
        self.assertRegex(bad["id"], r"^srv-\d+$")
        good = proc.request({"v": 1, "command": "system.info"})
        self.assertTrue(good["ok"], good)

    def test_blank_line_ignored(self) -> None:
        proc = self._proc()
        proc.write_bytes(b"\n")
        proc.write_bytes(b"   \n")
        answered = proc.request({"v": 1, "command": "system.info"})
        self.assertTrue(answered["ok"], answered)
        proc.expect_quiet(0.5)

    def test_oversize_line(self) -> None:
        # A line past --max-request-body-bytes (default 64 KiB) is rejected whole;
        # 05.0 owns the exact code, so either documented code is accepted.
        proc = self._proc()
        proc.send_envelope(
            {"v": 1, "command": "system.info", "args": {"x": "A" * 72000}})
        resp = proc.read_json(timeout=10.0)
        self.assertFalse(resp["ok"], resp)
        self.assertIn(resp["error"]["code"], ("invalid_envelope", "invalid_field"))
        alive = proc.request({"v": 1, "command": "system.info"})
        self.assertTrue(alive["ok"], alive)

    def test_crlf_framing(self) -> None:
        proc = self._proc()
        line = json.dumps({"v": 1, "command": "system.info"}).encode("utf-8")
        proc.write_bytes(line + b"\r\n")
        resp = proc.read_json(timeout=10.0)
        self.assertTrue(resp["ok"], resp)

    def test_eof_clean_exit(self) -> None:
        proc = self._proc()
        ack = proc.request({"v": 1, "command": "system.info"})
        self.assertTrue(ack["ok"], ack)
        proc.close_stdin()
        self.assertEqual(proc.wait(timeout=15.0), 0, proc.stderr_text())
        proc.expect_quiet(0.5)  # no partial output after the last envelope

    def test_job_progress_between_lines(self) -> None:
        proc = self._proc()
        data = "".join(
            "https://example.com/%d,job%05d\n" % (i, i) for i in range(400))
        accepted = proc.request(
            {"v": 1, "command": "link.import", "args": {"format": "csv", "data": data}})
        self.assertIn("job", accepted, accepted)
        job_id = accepted["job"]["id"]
        between = proc.request({"v": 1, "command": "system.info"})
        self.assertTrue(between["ok"], between)  # answered while the job runs
        deadline = time.monotonic() + 30.0
        while time.monotonic() < deadline:
            poll = proc.request({"v": 1, "command": "job.get", "args": {"id": job_id}})
            self.assertTrue(poll["ok"], poll)
            if poll["result"]["state"] in _TERMINAL:
                self.assertEqual(poll["result"]["state"], "succeeded", poll)
                return
            time.sleep(0.01)
        self.fail("import job did not reach a terminal state")

    def test_stderr_is_not_protocol(self) -> None:
        # serve-stdio exposes no verbosity/log-level flag (serve-stdio --help);
        # it always forces logs to stderr at the default level, which already
        # includes the mutating-command audit sink - the most sensitive log line
        # - so this default-level check IS the elevated-level proof.
        leak_marker = "zqx7leak3kv9probe"      # must never reach stderr
        slug = "stderrpuritycheck"             # audited resource id (control)
        url = "https://%s.example.invalid/%s" % (leak_marker, leak_marker)
        proc = self._proc()
        created = proc.request(
            {"v": 1, "id": "id-" + leak_marker, "command": "link.create",
             "args": {"target_url": url, "slug": slug}})
        self.assertTrue(created["ok"], created)   # the write really happened
        for req in ({"v": 1, "command": "command.list"},
                    {"v": 1, "command": "no.such_command"}):
            self.assertEqual(proc.request(req)["v"], 1)  # stdout is JSON only
        proc.close_stdin()
        self.assertEqual(proc.wait(timeout=15.0), 0, proc.stderr_text())
        stderr = proc.stderr_text()
        self.assertIn("serve_stdio_start", stderr)
        # Positive control: the audit path really ran, logging the slug as the
        # resource `target=` by design (threat brief 18); so stderr capture is
        # real and the absence check below is meaningful, not a no-op.
        self.assertIn("command_audit", stderr)
        self.assertIn(slug, stderr)
        # The one marker token - placed in the target URL and the envelope id,
        # neither of which the audit records - never leaks to stderr (C6).
        self.assertNotIn(leak_marker, stderr)


if __name__ == "__main__":
    unittest.main()
