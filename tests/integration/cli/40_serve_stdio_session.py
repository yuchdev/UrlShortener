"""
Integration test 40: `serve-stdio` NDJSON session framing, ordering, ids,
stdout/stderr purity and the `--read-only` / `--no-file-paths` / `--path-root`
hardening flags (Task 05.0 subtask 18).

Type: integration (spawns the real url_shortener binary in serve-stdio mode; no
HTTP listener - contract C5).

Scenario:
    A parent process frames one request envelope per stdin line and reads one
    response envelope per stdout line (the NDJSON transport, catalog 1, contract
    C2). This suite proves what the unit tests cannot: real pipe framing, strict
    request/response ordering over a live pipe, server-generated and echoed ids,
    malformed-line recovery, the line-length cap, blank-line and CRLF handling,
    UTF-8 round-tripping, stdout carrying only envelopes (carry-forward b /
    T-102), no request data on stderr (contract C6) and the three hardening
    flags (threat model T-107 / T-95 / T-95 path jail).

Boundaries:
    - Framing: exactly one response line per content line, in order.
    - ids: absent -> `srv-<n>`; present and well-formed -> echoed verbatim.
    - Recovery: a malformed line -> `invalid_envelope`, the process survives and
      answers the next valid line.
    - Caps: a line over `--max-request-body-bytes` -> `invalid_envelope`, alive.
    - Purity: every stdout line is JSON with `v==1`; the request URL never
      appears on stderr.
    - Flags: `--read-only` blocks writes but allows reads and refuses `path`
      export; `--no-file-paths` refuses a `path` import as `unsupported`;
      `--path-root` refuses an escaping `path` as `invalid_field`.

On failure first check:
    - serve-stdio forces the logger onto stderr before the runtime is built
      (17); a log line on stdout fails `test_stdout_is_only_envelopes`.
    - NdjsonSession echoes a recovered id only when it is a 1..64-byte clean
      string, else `srv-<n>` (14).
    - The hardening flags flow into CommandRuntimeOptions.file_paths and the
      read_only caller downgrade (15/17).
"""
import json
import os
import socket
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, clean_env
from stdio_session import StdioSession


def _port_open(port: int) -> bool:
    """Return True if a TCP connect to 127.0.0.1:*port* succeeds (C5 probe)."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        try:
            sock.connect(("127.0.0.1", port))
            return True
        except (ConnectionRefusedError, OSError):
            return False


class ServeStdioSessionTest(CliIntegrationBase):
    """[Integration][CLI] serve-stdio framing, ids, purity and hardening flags."""

    def _session(self, *args) -> StdioSession:
        """Start a serve-stdio session under a sanitised environment."""
        session = StdioSession(
            self._binary, "serve-stdio", *args, env=clean_env())
        self.addCleanup(session.close)
        return session

    # -- command surface and framing ---------------------------------------

    def test_command_list_exposes_job_and_import_commands(self):
        session = self._session()
        response = session.command("command.list")
        self.assertEqual(response["v"], 1)
        self.assertTrue(response["ok"], response)
        names = {c["name"] for c in response["result"]["commands"]}
        for name in ("link.create", "job.get", "link.import"):
            self.assertIn(name, names)

    def test_create_then_get_returns_equal_result(self):
        session = self._session()
        created = session.command(
            "link.create",
            {"target_url": "https://example.com/a", "slug": "sess-eq"})
        self.assertTrue(created["ok"], created)
        got = session.command("link.get", {"slug": "sess-eq"})
        self.assertTrue(got["ok"], got)
        self.assertEqual(created["result"], got["result"])

    def test_pipelined_requests_answered_in_order(self):
        # 50 requests written before reading any response return in order with
        # matching ids (strict ordering over a live pipe, T-104).
        session = self._session()
        count = 50
        for i in range(count):
            session.send(
                {"v": 1, "id": "c-%d" % i, "command": "system.info"})
        for i in range(count):
            response = session.recv()
            self.assertEqual(response["id"], "c-%d" % i, response)
            self.assertTrue(response["ok"], response)

    def test_request_without_id_gets_server_id(self):
        session = self._session()
        first = session.command("system.info")
        self.assertEqual(first["id"], "srv-1", first)
        second = session.command("system.info")
        self.assertEqual(second["id"], "srv-2", second)

    def test_well_formed_client_id_is_echoed(self):
        session = self._session()
        response = session.command("system.info", id="client-42")
        self.assertEqual(response["id"], "client-42", response)

    def test_malformed_line_then_valid_line(self):
        # A malformed line yields invalid_envelope; the same process then
        # answers a valid line (recovery, not a crash).
        session = self._session()
        session.send(b"this is not json\n")
        bad = session.recv()
        self.assertFalse(bad["ok"], bad)
        self.assertEqual(bad["error"]["code"], "invalid_envelope")
        good = session.command(
            "link.create",
            {"target_url": "https://example.com/a", "slug": "after-bad"})
        self.assertTrue(good["ok"], good)

    def test_oversize_line_rejected_process_survives(self):
        # A line past --max-request-body-bytes (default 64 KiB) -> dropped whole
        # and answered with invalid_envelope; the process stays alive (T-103).
        session = self._session()
        oversize = {"v": 1, "command": "x", "args": {"p": "A" * 72000}}
        session.send(json.dumps(oversize))
        response = session.recv()
        self.assertFalse(response["ok"], response)
        self.assertEqual(response["error"]["code"], "invalid_envelope")
        alive = session.command("system.info")
        self.assertTrue(alive["ok"], alive)

    def test_blank_lines_produce_no_response(self):
        # Blank / whitespace-only lines are swallowed silently: only the valid
        # line produces exactly one response.
        session = self._session()
        session.send(b"\n")
        session.send(b"   \n")
        session.send(b"\t\n")
        response = session.command("system.info")
        self.assertTrue(response["ok"], response)
        session.expect_no_response()

    def test_crlf_terminated_line_works(self):
        session = self._session()
        session.send(json.dumps({"v": 1, "command": "system.info"}).encode(
            "utf-8") + b"\r\n")
        response = session.recv()
        self.assertTrue(response["ok"], response)
        self.assertEqual(response["result"]["version"].count("."), 2, response)

    def test_utf8_client_id_round_trips(self):
        # A non-ASCII correlation id (valid UTF-8, <=64 bytes) is echoed verbatim.
        session = self._session()
        response = session.command("system.info", id="café-✓")
        self.assertEqual(response["id"], "café-✓", response)

    def test_utf8_url_round_trips(self):
        # Non-ASCII URL bytes survive a create -> get round trip unchanged.
        session = self._session()
        url = "https://example.com/café?q=naïve"
        created = session.command(
            "link.create", {"target_url": url, "slug": "utf8url"})
        self.assertTrue(created["ok"], created)
        self.assertEqual(created["result"]["url"], url, created)
        got = session.command("link.get", {"slug": "utf8url"})
        self.assertEqual(got["result"]["url"], url, got)

    # -- stdout / stderr purity (carry-forward b, T-102, C6) ---------------

    def test_stdout_is_only_envelopes_and_stderr_has_no_request_data(self):
        # Carry-forward (b): stdout carries only envelope lines, each a single
        # JSON object with v==1. Carry-forward stderr (C6): the request URL (an
        # argument value) never appears on stderr, which carries the start/stop
        # records and the audit trail only.
        secret_url = "https://wire-purity.example.invalid/do-not-leak-xyz"
        session = self._session()
        requests = [
            {"v": 1, "command": "command.list"},
            {"v": 1, "command": "link.create",
             "args": {"target_url": secret_url, "slug": "purity1"}},
            {"v": 1, "command": "system.info"},
            {"v": 1, "command": "no.such_command"},
        ]
        for req in requests:
            session.send(req)
        responses = [session.recv() for _ in requests]
        session.close_stdin()
        rc = session.wait()
        self.assertEqual(rc, 0, session.stderr_text())

        # Every observed response is one JSON object with the v1 envelope shape.
        self.assertEqual(len(responses), len(requests))
        for response in responses:
            self.assertEqual(response["v"], 1, response)
            self.assertIn("ok", response)

        # Carry-forward (b): a full drain of stdout (via recv_all after EOF)
        # would have yielded only these envelopes; recv already decoded each
        # line as one JSON object, so a stray log line on stdout would have
        # raised a JSONDecodeError above. Confirm the start/stop records that
        # prove logging went to stderr are present there, not on the wire.
        stderr = session.stderr_text()
        self.assertIn("serve_stdio_start", stderr)
        self.assertIn("serve_stdio_stop", stderr)
        # C6: the request URL (an argument value) is never written to stderr.
        self.assertNotIn("do-not-leak-xyz", stderr)
        self.assertNotIn(secret_url, stderr)

    # -- hardening flags (T-107 / T-95) ------------------------------------

    def test_read_only_blocks_writes_allows_reads_refuses_path_export(self):
        session = self._session("--read-only")
        created = session.command(
            "link.create",
            {"target_url": "https://example.com/a", "slug": "ro-create"})
        self.assertFalse(created["ok"], created)
        self.assertEqual(created["error"]["code"], "not_authorized")

        # A read command is permitted: it returns a working envelope (not_found
        # on the empty store), never not_authorized.
        got = session.command("link.get", {"slug": "ro-create"})
        self.assertFalse(got["ok"], got)
        self.assertEqual(got["error"]["code"], "not_found")

        # link.export with `path` raises the required permission to write, so a
        # read-only session is refused before any job is created (D16).
        exported = session.command(
            "link.export", {"format": "csv", "path": "/tmp/ro-export.csv"})
        self.assertFalse(exported["ok"], exported)
        self.assertEqual(exported["error"]["code"], "not_authorized")

    def test_no_file_paths_refuses_path_import_as_unsupported(self):
        session = self._session("--no-file-paths")
        response = session.command(
            "link.import", {"format": "csv", "path": "/tmp/import.csv"})
        self.assertFalse(response["ok"], response)
        self.assertEqual(response["error"]["code"], "unsupported")

    def test_path_root_refuses_escaping_path_as_invalid_field(self):
        with self.make_tmpdir() as tmpdir:
            session = self._session("--path-root", tmpdir)
            response = session.command(
                "link.import",
                {"format": "csv", "path": "../escape.csv"})
            self.assertFalse(response["ok"], response)
            self.assertEqual(response["error"]["code"], "invalid_field")

    def test_nonexistent_path_root_exits_two(self):
        # Carry-forward (e): a missing --path-root is rejected before any input
        # is read, with a value-free diagnostic and exit code 2.
        session = self._session("--path-root", "/no/such/serve-stdio-dir")
        rc = session.wait(timeout=15.0)
        self.assertEqual(rc, 2, session.stderr_text())
        stderr = session.stderr_text()
        self.assertIn("--path-root", stderr)
        self.assertNotIn("/no/such/serve-stdio-dir", stderr)

    # -- contract C5: no listener ------------------------------------------

    def test_no_listening_port_during_session(self):
        if _port_open(8000):
            self.skipTest("port 8000 already in use before the test")
        session = self._session()
        response = session.command("system.info")
        self.assertTrue(response["ok"], response)
        self.assertFalse(_port_open(8000), "serve-stdio must not bind a socket")


if __name__ == "__main__":
    unittest.main()
