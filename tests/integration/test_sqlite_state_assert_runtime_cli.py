"""Integration tests for runtime assertions in sqlite_state_assert CLI."""
from __future__ import annotations

import json
import socket
import sqlite3
import subprocess
import sys
import textwrap
import time
from pathlib import Path


REPO_ROOT = Path(__file__).parent.parent.parent


def _run_cli(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, "-m", "tools.sqlite_state_assert", *args],
        cwd=str(REPO_ROOT),
        capture_output=True,
        text=True,
    )


def _find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def _wait_port_open(port: int, timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            if s.connect_ex(("127.0.0.1", port)) == 0:
                return
        time.sleep(0.05)
    raise AssertionError(f"Timed out waiting for port {port} to open")


def _start_test_http_server(tmp_path: Path, port: int) -> subprocess.Popen[str]:
    script = tmp_path / "http_server.py"
    script.write_text(
        textwrap.dedent(
            f"""
            from http.server import BaseHTTPRequestHandler, HTTPServer

            class Handler(BaseHTTPRequestHandler):
                def do_GET(self):
                    if self.path == "/health":
                        self.send_response(200)
                        self.end_headers()
                        self.wfile.write(b"OK: healthy")
                        return
                    if self.path == "/demo":
                        self.send_response(302)
                        self.send_header("Location", "https://example.com")
                        self.end_headers()
                        return
                    self.send_response(404)
                    self.end_headers()

                def log_message(self, format, *args):
                    return

            server = HTTPServer(("127.0.0.1", {port}), Handler)
            server.serve_forever()
            """
        ),
        encoding="utf-8",
    )
    proc = subprocess.Popen([sys.executable, str(script)])
    _wait_port_open(port)
    return proc


class TestRuntimeAssertions:
    def test_port_open_and_closed(self, tmp_path):
        port = _find_free_port()
        proc = _start_test_http_server(tmp_path, port)
        try:
            open_result = _run_cli("--assert-port-open", str(port))
            assert open_result.returncode == 0, open_result.stderr
        finally:
            proc.terminate()
            proc.wait(timeout=10)

        closed_result = _run_cli("--assert-port-closed", str(port))
        assert closed_result.returncode == 0, closed_result.stderr

    def test_http_status_redirect_and_body(self, tmp_path):
        port = _find_free_port()
        proc = _start_test_http_server(tmp_path, port)
        try:
            status_result = _run_cli(
                "--assert-http-status",
                f"http://127.0.0.1:{port}/demo",
                "302",
            )
            assert status_result.returncode == 0, status_result.stderr

            redirect_result = _run_cli(
                "--assert-redirect-target",
                f"http://127.0.0.1:{port}/demo",
                "https://example.com",
            )
            assert redirect_result.returncode == 0, redirect_result.stderr

            body_result = _run_cli(
                "--assert-http-body-contains",
                f"http://127.0.0.1:{port}/health",
                "healthy",
            )
            assert body_result.returncode == 0, body_result.stderr
        finally:
            proc.terminate()
            proc.wait(timeout=10)

    def test_process_running(self):
        proc = subprocess.Popen(["sleep", "30"])
        try:
            result = _run_cli("--assert-process-running", "sleep")
            assert result.returncode == 0, result.stderr
        finally:
            proc.terminate()
            proc.wait(timeout=10)

    def test_log_contains(self, tmp_path):
        log_file = tmp_path / "server.log"
        log_file.write_text("Server started successfully\n", encoding="utf-8")
        result = _run_cli("--assert-log-contains", str(log_file), "Server started")
        assert result.returncode == 0, result.stderr


class TestResetAndFailureArtifacts:
    def test_reset_database_preserves_users_and_clears_transient_tables(self, tmp_path):
        db_path = tmp_path / "reset.sqlite"
        conn = sqlite3.connect(str(db_path))
        conn.executescript(
            """
            CREATE TABLE app_users(id TEXT PRIMARY KEY, username TEXT);
            CREATE TABLE links(short_code TEXT PRIMARY KEY, target_url TEXT);
            CREATE TABLE auth_sessions(id TEXT PRIMARY KEY, token_hash TEXT);
            INSERT INTO app_users VALUES ('u1', 'admin');
            INSERT INTO links VALUES ('abc', 'https://example.com');
            INSERT INTO auth_sessions VALUES ('s1', 'token');
            """
        )
        conn.commit()
        conn.close()

        result = _run_cli("--db", str(db_path), "--reset-database")
        assert result.returncode == 0, result.stderr

        conn = sqlite3.connect(str(db_path))
        assert conn.execute("SELECT COUNT(*) FROM app_users").fetchone()[0] == 1
        assert conn.execute("SELECT COUNT(*) FROM links").fetchone()[0] == 0
        assert conn.execute("SELECT COUNT(*) FROM auth_sessions").fetchone()[0] == 0
        conn.close()

    def test_failure_artifacts_created_on_assertion_failure(self, tmp_path):
        db_path = tmp_path / "failure.sqlite"
        sqlite3.connect(str(db_path)).close()
        log_file = tmp_path / "server.log"
        log_file.write_text("all good\n", encoding="utf-8")
        artifacts_root = tmp_path / "qa_failures"

        result = _run_cli(
            "--db",
            str(db_path),
            "--assert-log-contains",
            str(log_file),
            "Server started",
            "--failure-artifacts-dir",
            str(artifacts_root),
        )
        assert result.returncode == 1

        runs = sorted(artifacts_root.glob("*"))
        assert runs, "expected failure run directory"
        summary_path = runs[-1] / "summary.json"
        assert summary_path.exists()
        summary = json.loads(summary_path.read_text(encoding="utf-8"))
        assert summary["status"] == "fail"
        assert summary["operation"] == "assert_log_contains"
