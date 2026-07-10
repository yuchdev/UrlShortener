"""Shared utilities for Stage 1 integration test executables."""

import http.client
import os
import subprocess
import time


def find_server_path():
    """Resolve server binary path from environment or common build locations."""
    env_bin = os.environ.get("SIMPLE_HTTP_BIN")
    if env_bin and os.path.exists(env_bin):
        return env_bin

    script_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.abspath(os.path.join(script_dir, "..", "..", "build", "url_shortener")),
        os.path.abspath(os.path.join(script_dir, "..", "..", "cmake-build-debug", "url_shortener")),
        os.path.abspath(os.path.join(script_dir, "..", "..", "cmake-build-release", "url_shortener")),
    ]
    for candidate in candidates:
        if os.path.exists(candidate):
            return candidate
    raise RuntimeError("Server executable not found")


class ServerHarness:
    """Lifecycle helper for launching/shutting down server per integration test executable."""

    def __init__(self, args):
        self.args = args
        self.process = None

    def start(self):
        self.process = subprocess.Popen(
            [find_server_path()] + self.args,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(1.5)
        if self.process.poll() is not None:
            raise RuntimeError(
                f"Server failed to start (exit code {self.process.returncode})"
            )

    def stop(self):
        if self.process and self.process.poll() is None:
            self.process.terminate()
            self.process.wait(timeout=5)


def request(port, method, path, body=None, headers=None):
    """Issue HTTP request and return status, payload body, response object."""
    conn = http.client.HTTPConnection("localhost", port)
    conn.request(method, path, body=body, headers=headers or {})
    res = conn.getresponse()
    payload = res.read().decode("utf-8")
    conn.close()
    return res.status, payload, res
