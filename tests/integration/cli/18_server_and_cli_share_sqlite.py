"""
Integration test 18: a running server and one-shot CLI invocations share one
SQLite file (milestone 0006, task 02.0 subtask 14).

This is the executable form of the milestone property "the redirect path and the
command layer share one backend". A single server process (redirects + REST) and
separate ``url_shortener link ...`` CLI processes are all pointed at the same
``--storage-config <yaml>`` SQLite database. The test proves:

  * bidirectional visibility of *new* links is immediate (a fresh slug is never
    cached negatively): the server reads what the CLI wrote (step 2) and the CLI
    reads what the server wrote (step 4), both without any sleep;
  * a cross-process lifecycle change converges within the documented cache bound
    -- the server's L1 cache TTL is 5 s on persistent backends (02.0/11), so a
    CLI ``disable`` / ``delete`` of a *continuously hit* slug becomes visible to
    the server's redirect path within one TTL (+ slack). Convergence here is the
    direct acceptance test for threat-model T-32: ``recordRedirect`` must patch
    the L1 entry in place (preserving ``stored_at``) rather than ``set`` it, or
    polling (each poll is itself a redirect) would refresh the entry and the link
    would never expire;
  * the status codes and error bodies match the legacy in-memory server
    (carry-forward C3 from the 02.0/08 security review): disabled -> 410
    ``link_disabled``; soft-deleted -> 404 ``link_deleted``; restored -> 302;
  * the shared-file scenario completes with no unhandled "database is locked"
    error (threat model T-34; SQLite ``busy_timeout`` from 02.0/11);
  * write-behind redirect counters survive a graceful shutdown: 5 redirects
    followed by SIGTERM (-> ``backend.flush()``) persist ``total_redirects == 5``
    readable by a later CLI process (step 10). Skipped on Windows, where
    ``terminate()`` kills the process without running the signal handler.

Harness notes (flakiness controls -- server-spawning tests here run under
parallel ctest load):
  * a dynamic ephemeral port (``bind(("127.0.0.1", 0))``) avoids collisions with
    sibling tests that hard-code ports;
  * readiness is established by polling ``GET /healthz`` against a bounded
    deadline, never a fixed sleep;
  * convergence waits (steps 6/7) poll with a bounded deadline sized to the L1
    TTL (8 s = 5 s TTL + slack), never a fixed sleep;
  * the server is always terminated (then killed) in ``tearDownClass``;
  * a unique ``TemporaryDirectory`` holds the YAML and DB per run;
  * the server's stderr is redirected to a log file so a start failure is
    diagnosable (``ServerHarness`` discards it).

PREREQUISITE: the binary honors ``--storage-config`` in both server and CLI mode
(02.0/12), and ``recordRedirect`` patches L1 in place (02.0/01, 02.0/05, T-32).
"""
from __future__ import annotations

import http.client
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import (  # noqa: E402
    clean_env,
    find_binary,
    parse_json_stdout,
    run_cli,
    write_sqlite_storage_yaml,
)

# Readiness deadline for GET /healthz after spawning the server.
_READY_TIMEOUT_S = 10.0
# Cross-process convergence bound: L1 TTL (5 s on persistent backends) + slack.
_CONVERGE_TIMEOUT_S = 8.0
# Poll interval for both readiness and convergence waits.
_POLL_INTERVAL_S = 0.25


def _free_port() -> int:
    """Return a currently-free TCP port on the loopback interface.

    Binds to port 0, reads the kernel-assigned port, and closes the socket.
    A dynamic port keeps this server-spawning test from colliding with the
    hard-coded ports of sibling integration tests under parallel ctest load.
    """
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def _get(port: int, path: str):
    """Issue a GET to the loopback server without following redirects.

    :return: ``(status, location_header, body)``. ``location`` is ``None`` for
        non-redirect responses.
    """
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        conn.request("GET", path)
        res = conn.getresponse()
        body = res.read().decode("utf-8")
        return res.status, res.getheader("Location"), body
    finally:
        conn.close()


def _post(port: int, path: str, body: str):
    """Issue a JSON POST to the loopback server; return ``(status, body)``."""
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=5)
    try:
        conn.request(
            "POST", path, body=body, headers={"Content-Type": "application/json"}
        )
        res = conn.getresponse()
        payload = res.read().decode("utf-8")
        return res.status, payload
    finally:
        conn.close()


class ServerAndCliShareSqliteTest(unittest.TestCase):
    """
    [Integration][CLI] A running server and one-shot CLI processes share one
    SQLite file: new links are mutually visible immediately and lifecycle
    changes converge within the documented L1 cache bound.

    Scenario:
        Given a server started with ``--storage-config <yaml>`` and CLI
            invocations pointed at the same SQLite file,
        When links are created/mutated by one and read by the other,
        Then new links are visible immediately in both directions, a CLI
            disable/delete of a hot link becomes visible to the redirect path
            within one L1 TTL (status + error body matching the legacy server),
            a duplicate create conflicts, and redirect counters survive a
            graceful shutdown -- all with no "database is locked" error.

    Why this matters:
        This is the end-to-end proof that the redirect fast path and the command
        layer share one backend (02.0), including the T-32 hot-link TTL fix and
        the T-34 busy-timeout contract.
    """

    _binary: str = ""
    _tmp: tempfile.TemporaryDirectory
    _cfg: str
    _port: int
    _proc: subprocess.Popen
    _log_path: str

    @classmethod
    def setUpClass(cls) -> None:
        try:
            cls._binary = find_binary()
        except RuntimeError as exc:
            raise unittest.SkipTest(str(exc))

        cls._tmp = tempfile.TemporaryDirectory()
        tmpdir = cls._tmp.name
        cls._cfg = write_sqlite_storage_yaml(tmpdir)
        cls._port = _free_port()
        cls._log_path = os.path.join(tmpdir, "server.stderr.log")

        env = clean_env()
        log = open(cls._log_path, "w", encoding="utf-8")
        cls._log_handle = log
        cls._proc = subprocess.Popen(
            [
                cls._binary,
                "--storage-config", cls._cfg,
                "--http-port", str(cls._port),
                "--tls-enabled", "false",
                "--shortener-base-domain", "http://sho.rt",
            ],
            stdout=subprocess.DEVNULL,
            stderr=log,
            cwd=tmpdir,
            env=env,
        )

        if not cls._wait_for_server():
            rc = cls._proc.poll()
            cls._stop_server()
            log.flush()
            log_text = Path(cls._log_path).read_text(
                encoding="utf-8", errors="replace"
            )
            cls._tmp.cleanup()
            raise RuntimeError(
                "server did not become ready on /healthz within "
                f"{_READY_TIMEOUT_S}s (exit before readiness={rc}); "
                f"stderr:\n{log_text}"
            )

    @classmethod
    def _wait_for_server(cls) -> bool:
        """Poll ``GET /healthz`` until 200 or the readiness deadline; abort early
        if the process exits."""
        deadline = time.monotonic() + _READY_TIMEOUT_S
        while time.monotonic() < deadline:
            if cls._proc.poll() is not None:
                return False
            try:
                status, _, _ = _get(cls._port, "/healthz")
                if status == 200:
                    return True
            except OSError:
                pass
            time.sleep(_POLL_INTERVAL_S)
        return False

    @classmethod
    def _stop_server(cls) -> None:
        """Terminate (SIGTERM) then kill the server; idempotent."""
        proc = getattr(cls, "_proc", None)
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=8)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

    @classmethod
    def tearDownClass(cls) -> None:
        cls._stop_server()
        handle = getattr(cls, "_log_handle", None)
        if handle is not None:
            handle.close()
        tmp = getattr(cls, "_tmp", None)
        if tmp is not None:
            tmp.cleanup()

    # -- CLI convenience -------------------------------------------------

    def _cli(self, *args: str):
        """Run a one-shot CLI command against the shared SQLite config."""
        return run_cli(
            self._binary,
            *args,
            "--storage-config", self._cfg,
            cwd=self._tmp.name,
            env=clean_env(),
        )

    def _assert_no_db_locked(self, proc, where: str) -> None:
        """Fail if a CLI process surfaced a SQLite "database is locked" error
        (threat model T-34: the shared-file mode must not hard-fail)."""
        combined = (proc.stdout or "") + (proc.stderr or "")
        self.assertNotIn(
            "database is locked", combined.lower(),
            f"{where}: unhandled 'database is locked' (T-34); "
            f"stdout={proc.stdout!r} stderr={proc.stderr!r}",
        )

    # -- The scenario ----------------------------------------------------

    def test_server_and_cli_share_sqlite(self) -> None:
        """Steps 1-10 of the shared-SQLite scenario, run sequentially against a
        single server process and the same SQLite file."""
        port = self._port

        # Step 1: CLI creates share-a; exit 0, JSON slug == share-a.
        create_a = self._cli(
            "link", "create", "--url", "https://example.com/a", "--slug", "share-a"
        )
        self._assert_no_db_locked(create_a, "step 1 (CLI create share-a)")
        self.assertEqual(
            create_a.returncode, 0,
            f"step 1: create share-a failed: stderr={create_a.stderr!r}",
        )
        self.assertEqual(parse_json_stdout(create_a).get("slug"), "share-a")

        # Step 2: the server reads what the CLI wrote -- no sleep (a new slug is
        # never cached negatively). 302 to the exact target.
        status, location, _ = _get(port, "/share-a")
        self.assertEqual(
            status, 302,
            "step 2: server must resolve the CLI-created slug immediately "
            f"(got {status}); a fresh slug must not be cached negatively.",
        )
        self.assertEqual(location, "https://example.com/a")

        # Step 3: the server creates share-b via REST; 201.
        rest_status, rest_body = _post(
            port, "/api/v1/links",
            '{"url":"https://example.com/b","slug":"share-b"}',
        )
        self.assertEqual(
            rest_status, 201,
            f"step 3: REST create share-b expected 201, got {rest_status}: {rest_body!r}",
        )

        # Step 4: the CLI reads what the server wrote -- no sleep. exit 0.
        get_b = self._cli("link", "get", "--slug", "share-b")
        self._assert_no_db_locked(get_b, "step 4 (CLI get share-b)")
        self.assertEqual(
            get_b.returncode, 0,
            "step 4: CLI must read the server-created slug immediately: "
            f"stderr={get_b.stderr!r}",
        )
        self.assertEqual(parse_json_stdout(get_b).get("slug"), "share-b")

        # Step 5: CLI disables share-a; exit 0.
        disable_a = self._cli("link", "disable", "--slug", "share-a")
        self._assert_no_db_locked(disable_a, "step 5 (CLI disable share-a)")
        self.assertEqual(
            disable_a.returncode, 0,
            f"step 5: disable share-a failed: stderr={disable_a.stderr!r}",
        )

        # Step 6: the disable converges to 410 link_disabled within the L1 TTL.
        # Each poll is itself a redirect, so this also exercises the T-32
        # patch-in-place behavior (a hot link must still expire). The status may
        # legitimately still be 302 at first (documented staleness) -- the
        # assertion is that it *reaches* 410 within the bound, not instantly.
        # Poll explicitly so we can capture the body and the convergence time.
        t0 = time.monotonic()
        disabled_status = None
        disabled_body = None
        deadline = time.monotonic() + _CONVERGE_TIMEOUT_S
        while True:
            disabled_status, _, disabled_body = _get(port, "/share-a")
            if disabled_status == 410:
                break
            self.assertIn(
                disabled_status, (302, 410),
                f"step 6: unexpected status {disabled_status} while waiting for "
                "the disable to converge (expected 302 then 410).",
            )
            if time.monotonic() >= deadline:
                break
            time.sleep(_POLL_INTERVAL_S)
        self._disable_converge_s = time.monotonic() - t0
        self.assertEqual(
            disabled_status, 410,
            "step 6: a CLI disable of a continuously-hit slug did not converge "
            f"to 410 within {_CONVERGE_TIMEOUT_S}s (got {disabled_status}). "
            "If it stays 302, recordRedirect is refreshing the L1 TTL on each "
            "redirect (threat model T-32: recordRedirect must patch L1 in place, "
            "not set it) -- see src/app/repository_link_store.cpp recordRedirect.",
        )
        self.assertIn(
            '"code":"link_disabled"', disabled_body,
            "step 6: disabled redirect body must carry the link_disabled error "
            f"code (C3 parity with the legacy server): {disabled_body!r}",
        )

        # Step 7: CLI soft-deletes share-a; converges to 404 link_deleted.
        delete_a = self._cli("link", "delete", "--slug", "share-a")
        self._assert_no_db_locked(delete_a, "step 7 (CLI delete share-a)")
        self.assertEqual(
            delete_a.returncode, 0,
            f"step 7: delete share-a failed: stderr={delete_a.stderr!r}",
        )
        t1 = time.monotonic()
        deleted_status = None
        deleted_body = None
        deadline = time.monotonic() + _CONVERGE_TIMEOUT_S
        while True:
            deleted_status, _, deleted_body = _get(port, "/share-a")
            if deleted_status == 404:
                break
            self.assertIn(
                deleted_status, (410, 404),
                f"step 7: unexpected status {deleted_status} while waiting for "
                "the delete to converge (expected 410 then 404).",
            )
            if time.monotonic() >= deadline:
                break
            time.sleep(_POLL_INTERVAL_S)
        self._delete_converge_s = time.monotonic() - t1
        self.assertEqual(
            deleted_status, 404,
            "step 7: a CLI delete of a hot slug did not converge to 404 within "
            f"{_CONVERGE_TIMEOUT_S}s (got {deleted_status}); see T-32.",
        )
        self.assertIn(
            '"code":"link_deleted"', deleted_body,
            "step 7: deleted redirect body must carry the link_deleted error "
            f"code (C3 parity): {deleted_body!r}",
        )

        # Step 8: the server still serves share-b via REST (management plane).
        b_status, _, b_body = _get(port, "/api/v1/links/share-b")
        self.assertEqual(
            b_status, 200,
            f"step 8: REST GET share-b expected 200, got {b_status}: {b_body!r}",
        )
        self.assertEqual(
            json.loads(b_body).get("slug"), "share-b",
            f"step 8: REST GET share-b body slug mismatch: {b_body!r}",
        )

        # Step 9: a duplicate CLI create of share-b conflicts (exit 3).
        dup_b = self._cli(
            "link", "create", "--url", "https://example.com/dup", "--slug", "share-b"
        )
        self._assert_no_db_locked(dup_b, "step 9 (CLI duplicate create share-b)")
        self.assertEqual(
            dup_b.returncode, 3,
            "step 9: re-creating an existing slug must exit 3 (slug_conflict); "
            f"stdout={dup_b.stdout!r} stderr={dup_b.stderr!r}",
        )

        # Step 10 (POSIX only): 5 redirects of share-b, then SIGTERM -> the
        # server's shutdown flush persists the write-behind counter; a later CLI
        # read sees total_redirects == 5.
        if os.name == "nt":
            self.skipTest(
                "step 10 needs a graceful SIGTERM shutdown to flush write-behind "
                "counters; Windows terminate() kills the process without running "
                "the handler."
            )
        for i in range(5):
            r_status, _, _ = _get(port, "/share-b")
            self.assertEqual(
                r_status, 302,
                f"step 10: redirect {i + 1}/5 of share-b expected 302, got {r_status}.",
            )
        self._stop_server()
        # The server is stopped; confirm it exited cleanly (the flush ran).
        self.assertIsNotNone(
            self._proc.poll(),
            "step 10: server did not exit after terminate(); flush may not have run.",
        )
        final_get = self._cli("link", "get", "--slug", "share-b")
        self._assert_no_db_locked(final_get, "step 10 (CLI get after shutdown)")
        self.assertEqual(
            final_get.returncode, 0,
            f"step 10: CLI get share-b after shutdown failed: stderr={final_get.stderr!r}",
        )
        stats = parse_json_stdout(final_get).get("stats", {})
        self.assertEqual(
            stats.get("total_redirects"), 5,
            "step 10: the server's shutdown flush must persist exactly 5 "
            f"redirects for share-b; got {stats.get('total_redirects')!r}. A lower "
            "value means backend.flush() on SIGTERM did not persist the "
            "write-behind delta (src/main.cpp signal_handler -> backend.flush()).",
        )


if __name__ == "__main__":
    unittest.main()
