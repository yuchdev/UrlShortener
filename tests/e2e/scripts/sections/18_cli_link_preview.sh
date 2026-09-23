#!/usr/bin/env bash
# tests/e2e/scripts/sections/18_cli_link_preview.sh
#
# E2E CLI section 18: `link preview` must exit promptly without ever binding a
# listening socket (plan.md C4; Task 03.0 subtask 03).
#
# Mirrors 13_cli_no_server_socket.sh's port-check pattern for the preview verb.
# A non-zero exit from the command itself is acceptable here (the slug may not
# exist in a fresh in-memory process); this section proves the
# lifetime/socket guarantee, not the command's success payload (Task 04.0).
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

echo "=== 18_cli_link_preview ==="

if _port_open 8000; then
  echo "SKIP: port 8000 is already in use before the test; skipping to avoid false failure."
  exit 0
fi

if ! timeout 5 "$BINARY" link preview \
       --slug e2e-preview-slug \
       --base-domain http://sho.rt \
       >/dev/null 2>&1; then
  rc=$?
  if [[ "$rc" -eq 124 ]]; then
    echo "FAIL: 'link preview' did not exit within 5s (lifetime guarantee)." >&2
    exit 1
  fi
fi

sleep 0.3

if _port_open 8000; then
  echo "FAIL: port 8000 is OPEN after 'link preview' exited." >&2
  echo "  The binary started the HTTP server in CLI mode." >&2
  exit 1
fi

echo "PASS: 18_cli_link_preview"
