"""
Integration test 17: storage-config bootstrap errors and main.cpp wiring order
(milestone 0006, task 02.0 subtask 12).

These tests exercise the one point in main() where InitLinkBackend runs -- after
argument parsing and --help, before the CLI dispatch and before any HttpServer
is constructed -- so that:

  * a bad --storage-config fails fast with a stable exit code (4) and a
    sanitized stderr message that never leaks a DSN, a credential, or a
    filesystem path (threat model T-31);
  * CLI mode still never opens a listening socket (plan.md C5);
  * --help / `link --help` short-circuit *before* any storage file is touched
    (ordering rule 3), proving InitLinkBackend is not reached on the help path;
  * a good SQLite config produces exactly one JSON line on stdout and nothing
    from the bootstrap (ordering rule 4), and still binds no server port.

PREREQUISITE: main.cpp calls InitLinkBackend before DispatchLinkCommand and
before HttpServer construction (subtask 02.0/12).
"""
import os
import socket
import subprocess
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli

# A recognizable fake token planted inside a DSN so the leak test can assert it
# never reaches stderr. It is assembled from parts and is not a real credential.
_LEAK_PROBE = "leakprobe" + "XYZ" + "9999"
# Default plaintext HTTP port the server binds in server mode (ServerConfig
# http_port = 8000); a successful CLI command must never open it.
_SERVER_PORT = 8000


def _port_is_open(port: int, host: str = "127.0.0.1") -> bool:
    """Return True if a TCP socket is accepting connections on host:port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        try:
            sock.connect((host, port))
            return True
        except (ConnectionRefusedError, OSError, TimeoutError):
            return False


class StorageConfigErrorsTest(CliIntegrationBase):
    """
    [Integration][CLI] --storage-config bootstrap failures map to clean stderr
    and a stable exit code, without starting a listener or leaking secrets.

    Scenario:
        Given a CLI `link <verb>` invocation with a bad --storage-config,
        When the process runs,
        Then it exits 4, writes nothing to stdout, and writes only a fixed,
             value-free message to stderr.

    Why this matters:
        main() must bootstrap storage at one ordered point for every mode; a
        startup failure must be a deterministic, script-friendly exit code and
        must never surface a DSN/credential/path (CLAUDE.md secret rules, T-31).

    If this breaks, first check:
        - main.cpp calls InitLinkBackend before DispatchLinkCommand and before
          HttpServer construction, mapping std::runtime_error -> exit 4.
        - InitLinkBackend's thrown messages stay value-free (subtask 11).
    """

    def _write_cfg(self, tmpdir: str, name: str, body: str) -> str:
        path = os.path.join(tmpdir, name)
        with open(path, "w", encoding="ascii") as handle:
            handle.write(body)
        return path

    # -- CLI mode: bootstrap failures ------------------------------------

    def test_missing_config_file_exits_4(self):
        """A missing --storage-config file -> exit 4, empty stdout, stderr
        begins with `storage config:`."""
        with self.make_tmpdir() as tmpdir:
            missing = os.path.join(tmpdir, "does_not_exist.yaml")
            proc = run_cli(
                self._binary,
                "link", "get", "--slug", "x",
                "--storage-config", missing,
                cwd=tmpdir,
            )
            self.assertEqual(proc.returncode, 4, proc.stderr)
            self.assertEqual(proc.stdout.strip(), "")
            self.assertTrue(
                proc.stderr.startswith("storage config:"),
                f"stderr did not start with 'storage config:': {proc.stderr!r}",
            )

    def test_invalid_backend_name_names_backend_not_file(self):
        """An unknown backend name -> exit 4; stderr names the backend, never
        the config file path."""
        with self.make_tmpdir() as tmpdir:
            cfg = self._write_cfg(
                tmpdir, "bad_backend.yaml", "metadata:\n  backend: bogusdb\n"
            )
            proc = run_cli(
                self._binary,
                "link", "get", "--slug", "x",
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertEqual(proc.returncode, 4, proc.stderr)
            self.assertEqual(proc.stdout.strip(), "")
            self.assertIn("bogusdb", proc.stderr)
            self.assertNotIn(cfg, proc.stderr)
            self.assertNotIn("bad_backend.yaml", proc.stderr)

    def test_malformed_redis_port_exits_4_without_echo(self):
        """A redis cache whose redis_address has a non-numeric port makes the
        adapter factory throw a non-runtime_error; it must still exit 4 with a
        fixed message and never echo the address (security review LOW on
        02.0/12)."""
        with self.make_tmpdir() as tmpdir:
            cfg = self._write_cfg(
                tmpdir,
                "bad_redis.yaml",
                "metadata:\n  backend: inmemory\n"
                "cache:\n  backend: redis\n"
                "  redis_address: cachehost-marker:notaport\n",
            )
            proc = run_cli(
                self._binary,
                "link", "get", "--slug", "x",
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertEqual(proc.returncode, 4, proc.stderr)
            self.assertEqual(proc.stdout.strip(), "")
            self.assertNotIn("cachehost-marker", proc.stderr)
            self.assertNotIn("notaport", proc.stderr)

    def test_sqlite_path_in_missing_directory_backend_unavailable(self):
        """A SQLite path under a nonexistent directory -> exit 4, stderr
        `storage backend unavailable` (no path echoed)."""
        with self.make_tmpdir() as tmpdir:
            db_path = os.path.join(tmpdir, "no_such_dir", "s.db")
            cfg = self._write_cfg(
                tmpdir,
                "bad_dir.yaml",
                "metadata:\n  backend: sqlite\n  sqlite_path: "
                + db_path.replace("\\", "/")
                + "\n",
            )
            proc = run_cli(
                self._binary,
                "link", "create", "--url", "https://example.com",
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertEqual(proc.returncode, 4, proc.stderr)
            self.assertEqual(proc.stdout.strip(), "")
            self.assertIn("storage backend unavailable", proc.stderr)
            self.assertNotIn("no_such_dir", proc.stderr)

    def test_parse_error_does_not_leak_dsn_secret(self):
        """A malformed YAML whose postgres_dsn holds a recognizable fake
        credential must never surface that token (or the DSN) on stderr
        (T-31)."""
        with self.make_tmpdir() as tmpdir:
            # A tab-indented key is invalid YAML and forces a parser error on a
            # line adjacent to the DSN scalar. The DSN is split across source
            # lines so no single source line forms a full credential URL.
            dsn = "postgresql://user:" + _LEAK_PROBE + "@db.internal:5432/app"
            body = (
                "metadata:\n"
                "  backend: postgres\n"
                "  postgres_dsn: " + dsn + "\n"
                "\tbroken: [\n"
            )
            cfg = self._write_cfg(tmpdir, "leaky.yaml", body)
            proc = run_cli(
                self._binary,
                "link", "get", "--slug", "x",
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertNotEqual(proc.returncode, 0)
            self.assertEqual(proc.stdout.strip(), "")
            self.assertNotIn(_LEAK_PROBE, proc.stderr)
            self.assertNotIn("db.internal", proc.stderr)
            self.assertNotIn("postgresql://", proc.stderr)

    # -- Ordering rule 3: --help never touches storage -------------------

    def test_help_with_bogus_config_exits_zero(self):
        """`--help` with a bogus --storage-config -> exit 0 (no file access),
        proving InitLinkBackend runs after the help branch."""
        with self.make_tmpdir() as tmpdir:
            missing = os.path.join(tmpdir, "nope.yaml")
            proc = run_cli(
                self._binary, "--help", "--storage-config", missing, cwd=tmpdir
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(proc.stderr.strip(), "")

    def test_link_help_with_bogus_config_exits_zero(self):
        """`link --help` with a bogus --storage-config -> exit 0."""
        with self.make_tmpdir() as tmpdir:
            missing = os.path.join(tmpdir, "nope.yaml")
            proc = run_cli(
                self._binary,
                "link", "--help", "--storage-config", missing,
                cwd=tmpdir,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertEqual(proc.stderr.strip(), "")

    # -- Ordering rules 4/5: good SQLite config, no listener -------------

    def test_good_sqlite_config_one_json_line_no_port(self):
        """A valid SQLite config -> exit 0, exactly one JSON line on stdout,
        nothing from the bootstrap, and no server port bound (C5)."""
        if _port_is_open(_SERVER_PORT):
            self.skipTest(
                f"Port {_SERVER_PORT} already in use before the test; skipping."
            )
        with self.make_tmpdir() as tmpdir:
            db_path = os.path.join(tmpdir, "good.db").replace("\\", "/")
            cfg = self._write_cfg(
                tmpdir,
                "good.yaml",
                "metadata:\n  backend: sqlite\n  sqlite_path: " + db_path + "\n",
            )
            proc = run_cli(
                self._binary,
                "link", "create", "--url", "https://example.com",
                "--slug", "wired1",
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            lines = [ln for ln in proc.stdout.splitlines() if ln.strip()]
            self.assertEqual(
                len(lines), 1,
                f"expected exactly one JSON line, got: {proc.stdout!r}",
            )
            import json
            record = json.loads(lines[0])
            self.assertEqual(record["slug"], "wired1")
            # The bootstrap must print nothing in CLI mode.
            self.assertEqual(proc.stderr.strip(), "")
            time.sleep(0.2)
            self.assertFalse(
                _port_is_open(_SERVER_PORT),
                f"Port {_SERVER_PORT} open after CLI create with a storage "
                "config -- bootstrap must not start the HTTP server.",
            )


class ServerModeBadStorageConfigTest(CliIntegrationBase):
    """
    [Integration][CLI] Server mode with a bad --storage-config fails fast and
    does not open its HTTP port.

    Scenario:
        Given `url_shortener --storage-config <missing>` (server mode),
        When it runs,
        Then it exits non-zero within a few seconds and never binds port 8000.

    Why this matters:
        InitLinkBackend runs before HttpServer is constructed; a storage
        failure must abort startup before any socket is opened (plan.md C5,
        threat model: InitLinkBackend before HttpServer).

    If this breaks, first check:
        - main.cpp constructs the backend handle before io_context/HttpServer.
        - The storage-failure path returns before server.run().
    """

    def test_server_bad_config_exits_and_binds_no_port(self):
        if _port_is_open(_SERVER_PORT):
            self.skipTest(
                f"Port {_SERVER_PORT} already in use before the test; skipping."
            )
        with self.make_tmpdir() as tmpdir:
            missing = os.path.join(tmpdir, "server_missing.yaml")
            proc = subprocess.Popen(
                [
                    self._binary,
                    "--storage-config", missing,
                    "--tls-enabled", "false",
                ],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                cwd=tmpdir,
            )
            try:
                stdout, stderr = proc.communicate(timeout=8)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.communicate()
                self.fail(
                    "server did not exit within timeout for a bad storage "
                    "config (it may have started the listener)."
                )
            self.assertNotEqual(
                proc.returncode, 0,
                "server mode with a bad storage config should exit non-zero.",
            )
            self.assertEqual(stdout.strip(), "")
            self.assertTrue(
                stderr.startswith("storage config:"),
                f"unexpected server stderr: {stderr!r}",
            )
            time.sleep(0.2)
            self.assertFalse(
                _port_is_open(_SERVER_PORT),
                f"Port {_SERVER_PORT} open after a failed storage bootstrap -- "
                "the server must not bind before InitLinkBackend succeeds.",
            )


if __name__ == "__main__":
    unittest.main()
