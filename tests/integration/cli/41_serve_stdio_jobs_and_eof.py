"""
Integration test 41: `serve-stdio` job lifecycle, EOF drain, broken-pipe exit
code, no listener and cross-process persistence (Task 05.0 subtask 18).

Type: integration (spawns the real url_shortener binary in serve-stdio mode; no
HTTP listener - contract C5).

Scenario:
    Jobs ride the same NDJSON session as synchronous commands: link.import and
    link.export return a job envelope, job.get polls to a terminal state, and
    job.cancel stops a long import cooperatively. EOF on stdin is the only
    graceful stop (15); it drains in-flight jobs before the process exits 0. A
    closed stdout is mapped to exit 4 (SIGPIPE ignored, carry-forward a). With a
    SQLite --storage-config the imported rows survive into a second process,
    which proves the EOF drain ran the job to completion (carry-forward d) and
    that two sessions share one backend (carry-forward f).

Boundaries:
    - import: inline CSV -> job envelope; job.get reaches succeeded with
      matching counts; a mixed good/bad batch reports per-line errors.
    - cancel: a long path import cancelled on the first poll ends cancelled with
      created+skipped+failed <= total and a partial result (or succeeded if the
      machine finished first - accepted and logged).
    - export: inline export data re-imports into a second session losslessly.
    - EOF: import then immediate stdin close -> exit 0 after the job completes;
      with persistence, a later process sees the rows.
    - broken stdout: the reader closes its end mid-session -> exit 4, never a
      signal death and never a hang (POSIX).
    - no listener: port 8000 stays closed during and after the session (C5).

On failure first check:
    - StdioAdapter maps session_end::eof -> drain_jobs(graceful) -> 0 and
      output_failed -> 4 (15); RunServeStdio ignores SIGPIPE on POSIX (17).
    - The process-wide JobRunner is shared across runtimes/sessions (20, C11), so
      an import drains at EOF and persists through the configured backend.
"""
import json
import os
import socket
import subprocess
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import (
    CliIntegrationBase,
    clean_env,
    write_sqlite_storage_yaml,
)
from stdio_session import StdioSession

# Bounded poll budget for a job to reach a terminal state (no fixed sleeps for
# ordering; a bug fails by timeout, spec "Constraints").
_JOB_POLL_SECONDS = 30.0
_TERMINAL = ("succeeded", "failed", "cancelled")


def _port_open(port: int) -> bool:
    """Return True if a TCP connect to 127.0.0.1:*port* succeeds (C5 probe)."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        try:
            sock.connect(("127.0.0.1", port))
            return True
        except (ConnectionRefusedError, OSError):
            return False


def _poll_job(session: StdioSession, job_id: str, budget=_JOB_POLL_SECONDS):
    """Poll job.get until the job reaches a terminal state or the budget ends.

    :return: The terminal ``result`` object of the job.get response.
    :raises AssertionError: The job did not terminate within *budget*.
    """
    deadline = time.monotonic() + budget
    last = None
    while time.monotonic() < deadline:
        response = session.command("job.get", {"id": job_id})
        assert response["ok"], response
        last = response["result"]
        if last["state"] in _TERMINAL:
            return last
        time.sleep(0.01)
    raise AssertionError("job %s did not terminate; last=%r" % (job_id, last))


def _export_pairs(csv_data: str):
    """Parse an exported CSV ``data`` string into a set of (url, slug) pairs,
    ignoring the header and blank trailing line."""
    pairs = set()
    for line in csv_data.splitlines():
        if not line or line == "full_url,slug":
            continue
        url, _, slug = line.partition(",")
        pairs.add((url, slug))
    return pairs


class ServeStdioJobsAndEofTest(CliIntegrationBase):
    """[Integration][CLI] serve-stdio jobs, EOF drain, broken pipe, no listener."""

    def _session(self, *args, env=None) -> StdioSession:
        session = StdioSession(
            self._binary, "serve-stdio", *args,
            env=env if env is not None else clean_env())
        self.addCleanup(session.close)
        return session

    def _probe_persistent(self, storage_yaml: str) -> bool:
        """Return system.info.persistent for a storage config (capability probe,
        catalog 3.5) so persistence assertions pass before and after Task 02.0."""
        session = self._session("--storage-config", storage_yaml)
        try:
            info = session.command("system.info")
            return bool(info["result"]["persistent"])
        finally:
            session.close()

    # -- import / job lifecycle --------------------------------------------

    def test_inline_import_job_reaches_succeeded_with_counts(self):
        session = self._session()
        rows = 500
        data = "".join(
            "https://example.com/%d,imp%05d\n" % (i, i) for i in range(rows))
        accepted = session.command("link.import", {"format": "csv", "data": data})
        self.assertTrue(accepted["ok"], accepted)
        self.assertIn("job", accepted, accepted)
        self.assertIn(accepted["job"]["state"], ("queued", "running", "succeeded"))

        result = _poll_job(session, accepted["job"]["id"])
        self.assertEqual(result["state"], "succeeded", result)
        counts = result["result"]
        self.assertEqual(counts["total"], rows, counts)
        self.assertEqual(counts["created"], rows, counts)
        self.assertEqual(counts["failed"], 0, counts)

    def test_mixed_batch_reports_per_line_errors(self):
        session = self._session()
        data = (
            "https://example.com/ok,goodrow\n"
            "not-a-url,badrow\n"
        )
        accepted = session.command("link.import", {"format": "csv", "data": data})
        self.assertIn("job", accepted, accepted)
        result = _poll_job(session, accepted["job"]["id"])
        self.assertEqual(result["state"], "succeeded", result)
        counts = result["result"]
        self.assertEqual(counts["total"], 2, counts)
        self.assertEqual(counts["created"], 1, counts)
        self.assertEqual(counts["failed"], 1, counts)
        self.assertTrue(counts["errors"], counts)
        self.assertEqual(counts["errors"][0]["line"], 2, counts)
        self.assertEqual(counts["errors"][0]["code"], "invalid_url", counts)

    def test_cancel_long_import_ends_cancelled_or_succeeded(self):
        # A long import read from a path (a 20k-row inline payload would exceed
        # the 64 KiB line cap), cancelled on the first poll. Cooperative cancel
        # keeps partial effects; if the machine finished the batch first the
        # succeeded terminal is accepted and logged.
        with self.make_tmpdir() as tmpdir:
            csv_path = os.path.join(tmpdir, "long.csv")
            total = 20000
            with open(csv_path, "w", encoding="utf-8") as handle:
                for i in range(total):
                    handle.write("https://example.com/%d,canc%05d\n" % (i, i))

            session = self._session()
            accepted = session.command(
                "link.import", {"format": "csv", "path": csv_path})
            self.assertIn("job", accepted, accepted)
            job_id = accepted["job"]["id"]

            cancelled = session.command("job.cancel", {"id": job_id})
            self.assertTrue(cancelled["ok"], cancelled)

            result = _poll_job(session, job_id)
            counts = result["result"]
            if result["state"] == "cancelled":
                self.assertLessEqual(
                    counts["created"] + counts["skipped"] + counts["failed"],
                    counts["total"], counts)
                self.assertIn("created", counts, counts)
            else:
                # Accepted fallback: the batch finished before the cancel landed.
                self.assertEqual(result["state"], "succeeded", result)
                sys.stderr.write(
                    "note: long import completed before cancel took effect\n")

    def test_export_inline_reimports_into_second_session(self):
        # Export from one session, re-import the exported data into a fresh
        # session, and confirm the (url, slug) pair sets are equal (lossless).
        producer = self._session()
        originals = {
            ("https://example.com/x1", "roundx1"),
            ("https://example.com/x2", "roundx2"),
        }
        for url, slug in sorted(originals):
            created = producer.command(
                "link.create", {"target_url": url, "slug": slug})
            self.assertTrue(created["ok"], created)
        accepted = producer.command("link.export", {"format": "csv"})
        self.assertIn("job", accepted, accepted)
        export_result = _poll_job(producer, accepted["job"]["id"])
        self.assertEqual(export_result["state"], "succeeded", export_result)
        data = export_result["result"]["data"]
        self.assertEqual(_export_pairs(data), originals)

        consumer = self._session()
        reimport = consumer.command("link.import", {"format": "csv", "data": data})
        self.assertIn("job", reimport, reimport)
        import_result = _poll_job(consumer, reimport["job"]["id"])
        self.assertEqual(import_result["state"], "succeeded", import_result)
        self.assertEqual(import_result["result"]["created"], len(originals))

        reexport = consumer.command("link.export", {"format": "csv"})
        reexport_result = _poll_job(consumer, reexport["job"]["id"])
        self.assertEqual(
            _export_pairs(reexport_result["result"]["data"]), originals)

    # -- EOF drain (carry-forward d) ---------------------------------------

    def test_running_job_completes_before_exit_same_session(self):
        # Carry-forward (d), within one session: a submitted import reaches
        # succeeded (polled in-session) before the session is closed.
        session = self._session()
        data = "".join(
            "https://example.com/d%d,drain%05d\n" % (i, i) for i in range(50))
        accepted = session.command("link.import", {"format": "csv", "data": data})
        result = _poll_job(session, accepted["job"]["id"])
        self.assertEqual(result["state"], "succeeded", result)
        self.assertEqual(result["result"]["created"], 50, result)
        session.close_stdin()
        self.assertEqual(session.wait(), 0, session.stderr_text())

    def test_import_then_immediate_eof_exits_zero(self):
        # Submit an import, close stdin immediately (do not poll): the EOF drain
        # waits for the job, then the process exits 0.
        session = self._session()
        data = "".join(
            "https://example.com/e%d,eofrow%05d\n" % (i, i) for i in range(50))
        accepted = session.command("link.import", {"format": "csv", "data": data})
        self.assertIn("job", accepted, accepted)
        session.close_stdin()
        self.assertEqual(session.wait(), 0, session.stderr_text())

    def test_eof_drain_persists_rows_cross_process(self):
        # Carry-forward (d) cross-process: with persistence, an import submitted
        # then abandoned at EOF has still run to completion - a second process
        # reading the same backend finds the rows. Guarded by the persistence
        # capability probe so it is a no-op before Task 02.0.
        with self.make_tmpdir() as tmpdir:
            storage_yaml = write_sqlite_storage_yaml(tmpdir)
            if not self._probe_persistent(storage_yaml):
                self.skipTest("backend is not persistent (pre-02.0)")

            producer = self._session("--storage-config", storage_yaml)
            data = "https://example.com/eofp,eofpersist\n"
            accepted = producer.command(
                "link.import", {"format": "csv", "data": data})
            self.assertIn("job", accepted, accepted)
            producer.close_stdin()
            self.assertEqual(producer.wait(), 0, producer.stderr_text())

            reader = self._session("--storage-config", storage_yaml)
            got = reader.command("link.get", {"slug": "eofpersist"})
            self.assertTrue(got["ok"], got)
            self.assertEqual(got["result"]["slug"], "eofpersist")

    # -- broken stdout (carry-forward a) -----------------------------------

    def test_closed_stdout_exits_four_not_signaled(self):
        # Carry-forward (a) / T-106 (POSIX): close the read end of stdout after
        # the first response, feed more requests, and confirm the child exits 4
        # (stream failure) rather than being killed by SIGPIPE or hanging. Run
        # with a raw Popen so the pipe can be closed deterministically.
        if sys.platform.startswith("win"):
            self.skipTest("SIGPIPE behaviour is POSIX-only")
        proc = subprocess.Popen(
            [self._binary, "serve-stdio"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            env=clean_env(),
            bufsize=0,
        )
        try:
            proc.stdin.write(b'{"v":1,"command":"system.info"}\n')
            proc.stdin.flush()
            self.assertTrue(proc.stdout.readline(), "expected a first response")
            # Break the wire: close the reader's end of stdout.
            proc.stdout.close()
            # Keep feeding requests so the child writes again and hits EPIPE.
            for _ in range(10):
                try:
                    proc.stdin.write(b'{"v":1,"command":"system.info"}\n')
                    proc.stdin.flush()
                except (BrokenPipeError, OSError):
                    break
                time.sleep(0.02)
            try:
                proc.stdin.close()
            except OSError:
                pass
            rc = proc.wait(timeout=15)
            self.assertEqual(rc, 4, "closed stdout must map to exit 4")
            self.assertGreaterEqual(rc, 0, "must not be killed by a signal")
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait(timeout=5)

    # -- no listener (C5) and path import ----------------------------------

    def test_no_listening_port_during_and_after_session(self):
        if _port_open(8000):
            self.skipTest("port 8000 already in use before the test")
        session = self._session()
        response = session.command("system.info")
        self.assertTrue(response["ok"], response)
        self.assertFalse(_port_open(8000), "no socket during the session (C5)")
        session.close_stdin()
        self.assertEqual(session.wait(), 0, session.stderr_text())
        self.assertFalse(_port_open(8000), "no socket after the session (C5)")

    def test_path_import_of_temp_file_succeeds(self):
        with self.make_tmpdir() as tmpdir:
            csv_path = os.path.join(tmpdir, "pairs.csv")
            with open(csv_path, "w", encoding="utf-8") as handle:
                handle.write("https://example.com/p1,pathrow1\n")
                handle.write("https://example.com/p2,pathrow2\n")
            session = self._session()
            accepted = session.command(
                "link.import", {"format": "csv", "path": csv_path})
            self.assertIn("job", accepted, accepted)
            result = _poll_job(session, accepted["job"]["id"])
            self.assertEqual(result["state"], "succeeded", result)
            self.assertEqual(result["result"]["created"], 2, result)

    # -- storage-config persistence (carry-forward f) ----------------------

    def test_storage_config_persists_across_two_sessions(self):
        # Carry-forward (f): a link created under --storage-config in session 1
        # is listed in a second session - the split-store fix of subtask 17.
        with self.make_tmpdir() as tmpdir:
            storage_yaml = write_sqlite_storage_yaml(tmpdir)
            if not self._probe_persistent(storage_yaml):
                self.skipTest("backend is not persistent (pre-02.0)")

            writer = self._session("--storage-config", storage_yaml)
            created = writer.command(
                "link.create",
                {"target_url": "https://example.com/persist", "slug": "persx"})
            self.assertTrue(created["ok"], created)
            writer.close_stdin()
            self.assertEqual(writer.wait(), 0, writer.stderr_text())

            reader = self._session("--storage-config", storage_yaml)
            listed = reader.command("link.list", {"status": "all"})
            self.assertTrue(listed["ok"], listed)
            slugs = {item["slug"] for item in listed["result"]["items"]}
            self.assertIn("persx", slugs)


if __name__ == "__main__":
    unittest.main()
