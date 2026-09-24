#!/usr/bin/env bash
# tests/e2e/scripts/sections/18_cli_link_preview.sh
#
# E2E CLI section 18: `link preview` must exit promptly without ever binding a
# listening socket (plan.md C4; Task 03.0 subtask 03).
#
# Mirrors 13_cli_no_server_socket.sh's port-check pattern for the preview verb.
# The command runs one-shot against a fresh in-memory process where the slug
# does not exist, so it must reach dispatch and return the not-found result
# (exit code 1 plus a "preview failed: Link not found" diagnostic on stderr).
# This section proves the lifetime/socket guarantee and dispatch reachability,
# not the command's success payload (Task 04.0).
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

# Capture stderr so we can assert the command reached dispatch (rather than
# failing earlier during argument parsing) and cleaned up on exit.
stderr_file="$(mktemp)"
trap 'rm -f "$stderr_file"' EXIT

# Capture timeout's own exit status directly (|| rc=$? both satisfies `set -e`
# and preserves the real code); `! timeout ...` would instead force rc=0 in the
# then-branch and make the 124 check unreachable.
rc=0
timeout 5 "$BINARY" link preview \
       --slug e2e-preview-slug \
       >/dev/null 2>"$stderr_file" || rc=$?
if [[ "$rc" -eq 124 ]]; then
  echo "FAIL: 'link preview' did not exit within 5s (lifetime guarantee)." >&2
  exit 1
fi

# The slug does not exist in a fresh in-memory process, so the command must
# reach dispatch and return the not-found result (exit code 1 plus the
# "preview failed: Link not found" diagnostic). Any other outcome (e.g. an
# argument-parsing error before dispatch) is a real failure of this section.
if [[ "$rc" -ne 1 ]] || ! grep -q "preview failed: Link not found" "$stderr_file"; then
  echo "FAIL: 'link preview' did not reach the dispatch not-found result (rc=$rc)." >&2
  cat "$stderr_file" >&2
  exit 1
fi

sleep 0.3

if _port_open 8000; then
  echo "FAIL: port 8000 is OPEN after 'link preview' exited." >&2
  echo "  The binary started the HTTP server in CLI mode." >&2
  exit 1
fi

echo "PASS: 18_cli_link_preview"
