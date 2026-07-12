#!/usr/bin/env python3
from http.server import BaseHTTPRequestHandler, HTTPServer
import json
from pathlib import Path

LOG = Path("tests/e2e/tmp/server.log")
LOG.parent.mkdir(parents=True, exist_ok=True)


def log(msg: str) -> None:
    with LOG.open("a", encoding="utf-8") as f:
        f.write(msg + "\n")


class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"OK")
            return
        if self.path == "/r/demo":
            self.send_response(302)
            self.send_header("Location", "https://example.com")
            self.end_headers()
            return
        if self.path == "/r/expired":
            self.send_response(410)
            self.end_headers()
            return
        if self.path == "/r/missing":
            self.send_response(404)
            self.end_headers()
            return
        if self.path == "/auth/forbidden":
            self.send_response(403)
            self.end_headers()
            return
        if self.path == "/admin/ping":
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"pong")
            return
        if self.path == "/admin/stats":
            self.send_response(200)
            self.end_headers()
            self.wfile.write(json.dumps({"total": 1}).encode("utf-8"))
            return
        self.send_response(404)
        self.end_headers()

    def log_message(self, fmt, *args):
        return


if __name__ == "__main__":
    log("Server started")
    HTTPServer(("127.0.0.1", 18080), Handler).serve_forever()
