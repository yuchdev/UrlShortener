"""
Integration test 10: running the binary in server mode (no 'link' subcommand)
is unaffected by the CLI additions.  Server mode starts the HTTP listener as
before; CLI mode does not.

PREREQUISITE: CLI subcommand dispatch must be implemented without breaking
the existing server-mode path.
"""
import http.client
import os
import subprocess
import sys
import time
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, find_binary

_SERVER_PORT = 28110


def _wait_for_port(port: int, host: str = "127.0.0.1", retries: int = 15) -> bool:
    import socket
    for _ in range(retries):
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(0.3)
            try:
                s.connect((host, port))
                return True
            except (ConnectionRefusedError, OSError, TimeoutError):
                time.sleep(0.2)
    return False


class ServerModeUnchangedByCliTest(CliIntegrationBase):
    """
    [Integration][CLI] Server mode starts the HTTP listener as before.

    Scenario:
        Given the binary started WITHOUT the 'link' subcommand (server mode).
        When it is given a port flag and the health endpoint is queried.
        Then the health endpoint returns 200.

    Scenario (separation):
        Given the binary started WITH 'link create' (CLI mode).
        When it exits.
        Then the HTTP port is NOT open.

    Why this matters:
        The CLI additions must not change the server startup path for existing
        deployments and CI jobs that start the server.

    If this breaks, first check:
        - main.cpp: CLI dispatch must NOT affect the server-mode branch.
        - ParseResult carries the correct mode (server vs CLI).
    """

    @classmethod
    def setUpClass(cls) -> None:
        # Use find_binary directly without CLI probe for the server-mode test.
        try:
            cls._binary = find_binary()
        except RuntimeError as exc:
            raise unittest.SkipTest(str(exc))
        # The CLI probe is intentionally skipped here; the server-mode
        # test is valid even if CLI is not yet implemented.

    def test_server_mode_starts_http_listener(self):
        proc = subprocess.Popen(
            [
                self._binary,
                "--http-port", str(_SERVER_PORT),
                "--tls-enabled", "false",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        try:
            alive = _wait_for_port(_SERVER_PORT)
            self.assertTrue(
                alive,
                f"Server did not open port {_SERVER_PORT} within timeout; "
                f"server mode may be broken by CLI refactoring.",
            )
            if alive:
                conn = http.client.HTTPConnection("127.0.0.1", _SERVER_PORT)
                conn.request("GET", "/health")
                resp = conn.getresponse()
                conn.close()
                self.assertEqual(
                    resp.status, 200,
                    f"Expected 200 from /health, got {resp.status}",
                )
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

    def test_cli_mode_does_not_start_http_listener(self):
        """Covered by test 04 (04_cli_exits_no_listening_port.py); referenced here
        for completeness of the separation contract."""
        self.skipTest("Separation covered in 04_cli_exits_no_listening_port.py")


if __name__ == "__main__":
    unittest.main()
