#!/usr/bin/env bash
# tests/e2e/scripts/sections/13_cli_no_server_socket.sh
#
# E2E CLI section 13: CLI mode must NOT leave a listening socket after exit.
#
# This script proves the separation between CLI mode and server mode:
# running `link create` must exit without ever binding port 8000 (or any
# other port configured by default).
#
# PREREQUISITES:
#   1. CLI dispatch in main.cpp: "link create" branch does NOT call server.run().
#   2. io_context.run() is NOT invoked in CLI mode.
#
# The check works by:
#   a) Confirming port 8000 is closed before the test.
#   b) Running `link create` (one-shot).
#   c) Confirming port 8000 is still closed after the process exits.
#
# Uses Python for socket probing (no external deps like netstat / ss).
set -euo pipefail

BINARY="${URLSHORTENER_BIN:-}"
if [[ -z "$BINARY" ]]; then
  for candidate in \
      cmake-build/url_shortener \
      cmake-build/Debug/url_shortener \
      build/url_shortener; do
    if [[ -x "$candidate" ]]; then
      BINARY="$candidate"
      break
    fi
  done
fi

if [[ -z "$BINARY" || ! -x "$BINARY" ]]; then
  echo "ERROR: url_shortener binary not found. Set URLSHORTENER_BIN or build first." >&2
  exit 2
fi

TMPDIR_CLI="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_CLI"' EXIT

_port_open() {
  local port="$1"
  python3 - "$port" <<'PY'
import socket, sys
port = int(sys.argv[1])
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.settimeout(0.5)
    try:
        s.connect(("127.0.0.1", port))
        sys.exit(0)          # port open
    except (ConnectionRefusedError, OSError, TimeoutError):
        sys.exit(1)          # port closed
PY
}

echo "=== 13_cli_no_server_socket ==="

# Pre-check: skip if port 8000 is already in use (conflict with another process).
if _port_open 8000; then
  echo "SKIP: port 8000 is already in use before the test; skipping to avoid false failure."
  exit 0
fi

# Run CLI link create and wait for exit.
"$BINARY" link create \
  --url https://no-server.example.com \
  --base-domain http://sho.rt \
  2>/dev/null || true   # non-zero exit is ok here if CLI errors; what matters is it exits

# Brief pause to allow any lingering socket teardown.
sleep 0.3

# Post-check: port 8000 must still be closed.
if _port_open 8000; then
  echo "FAIL: port 8000 is OPEN after 'link create' exited." >&2
  echo "  The binary started the HTTP server in CLI mode." >&2
  exit 1
fi

echo "PASS: 13_cli_no_server_socket"
