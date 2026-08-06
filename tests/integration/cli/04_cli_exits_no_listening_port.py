"""
Integration test 04: CLI mode exits after the command and does NOT leave a
listening server socket on port 8000 (or any other port).

This proves the separation between CLI mode and server mode: a one-shot
link command must never start the HTTP listener.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import socket
import subprocess
import sys
import os
import tempfile
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


def _port_is_open(port: int, host: str = "127.0.0.1") -> bool:
    """Return True if a TCP socket is accepting connections on host:port."""
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.settimeout(0.5)
        try:
            sock.connect((host, port))
            return True
        except (ConnectionRefusedError, OSError, TimeoutError):
            return False


class CliExitsNoListeningPortTest(CliIntegrationBase):
    """
    [Integration][CLI] CLI link command exits and does not bind a server port.

    Scenario:
        Given no server is running on port 8000.
        When: url_shortener link create --url https://example.com (CLI mode)
        Then: process exits (returncode is set) within timeout,
              and port 8000 is NOT accepting connections after the process exits.

    Why this matters:
        If CLI mode accidentally starts the server, automated test runners will
        hang, CI jobs will time out, and the port will conflict with a real server.

    If this breaks, first check:
        - main.cpp CLI dispatch exits WITHOUT calling server.run().
        - io_context.run() is NOT called in CLI mode.
    """

    _SERVER_PORT = 8000

    def test_cli_process_exits_promptly(self):
        """link create must return a CompletedProcess (i.e. not hang)."""
        with self.make_tmpdir() as tmpdir:
            # run_cli already has a timeout; if the process hangs it raises
            # subprocess.TimeoutExpired and the test fails with a clear message.
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://exitcheck.example.com",
                cwd=tmpdir,
                timeout=8,
            )
            # Any exit code is acceptable here; what matters is that it exited.
            self.assertIsNotNone(
                proc.returncode,
                "process did not exit – it is still running (server mode?)",
            )

    def test_cli_does_not_bind_default_port(self):
        """After link create exits, port 8000 must remain closed."""
        with self.make_tmpdir() as tmpdir:
            # Sanity: port should not be open before we start.
            if _port_is_open(self._SERVER_PORT):
                self.skipTest(
                    f"Port {self._SERVER_PORT} is already in use before the test; "
                    "skipping to avoid false failure."
                )

            run_cli(
                self._binary,
                "link", "create",
                "--url", "https://portcheck.example.com",
                cwd=tmpdir,
                timeout=8,
            )

            # Give the OS a moment for any port cleanup (should be instant for CLI).
            time.sleep(0.2)

            self.assertFalse(
                _port_is_open(self._SERVER_PORT),
                f"Port {self._SERVER_PORT} is open after CLI link create – "
                "the binary appears to have started the HTTP server.",
            )

    def test_cli_get_also_exits_promptly(self):
        """link get must also return promptly (not start a server)."""
        with self.make_tmpdir() as tmpdir:
            # Even if the slug doesn't exist, the process must exit.
            proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "nonexistent",
                cwd=tmpdir,
                timeout=8,
            )
            self.assertIsNotNone(proc.returncode)


if __name__ == "__main__":
    unittest.main()
