#!/usr/bin/env bash
# tests/e2e/scripts/sections/14_cli_link_update.sh
#
# E2E CLI section 14: `link update` must exit promptly without ever binding a
# listening socket (plan.md C4; Task 03.0 subtask 03).
#
# This mirrors 13_cli_no_server_socket.sh's port-check pattern for the update
# verb. The command runs one-shot and must:
#   a) not leave port 8000 (or any default port) open after it exits, and
#   b) exit well within the process-lifetime budget (a couple of seconds).
#
# The command runs one-shot against a fresh in-memory process where the slug
# does not exist, so it must reach dispatch and return the not-found result
# (exit code 1 plus a "update failed: Link not found" diagnostic on stderr).
# This section proves both the lifetime/socket guarantee and that the verb
# actually reaches dispatch, not the command's success payload (owned by
# Task 04.0).
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

echo "=== 14_cli_link_update ==="

# Pre-check: skip if port 8000 is already in use (conflict with another process).
if _port_open 8000; then
  echo "SKIP: port 8000 is already in use before the test; skipping to avoid false failure."
  exit 0
fi

# Capture stderr so we can assert the command reached dispatch (rather than
# failing earlier during argument parsing) and cleaned up on exit.
stderr_file="$(mktemp)"
trap 'rm -f "$stderr_file"' EXIT

# Run CLI link update one-shot and bound its runtime; the command must return on
# its own (never enter io_context.run()), so a timeout firing is itself a
# failure of the lifetime guarantee.
# Capture timeout's own exit status directly (|| rc=$? both satisfies `set -e`
# and preserves the real code); `! timeout ...` would instead force rc=0 in the
# then-branch and make the 124 check unreachable.
rc=0
timeout 5 "$BINARY" link update \
       --slug e2e-update-slug \
       --enabled false \
       >/dev/null 2>"$stderr_file" || rc=$?
if [[ "$rc" -eq 124 ]]; then
  echo "FAIL: 'link update' did not exit within 5s (lifetime guarantee)." >&2
  exit 1
fi

# The slug does not exist in a fresh in-memory process, so the command must
# reach dispatch and return the not-found result (exit code 1 plus the
# "update failed: Link not found" diagnostic). Any other outcome (e.g. an
# argument-parsing error before dispatch) is a real failure of this section.
if [[ "$rc" -ne 1 ]] || ! grep -q "update failed: Link not found" "$stderr_file"; then
  echo "FAIL: 'link update' did not reach the dispatch not-found result (rc=$rc)." >&2
  cat "$stderr_file" >&2
  exit 1
fi

# Brief pause to allow any lingering socket teardown.
sleep 0.3

# Post-check: port 8000 must still be closed.
if _port_open 8000; then
  echo "FAIL: port 8000 is OPEN after 'link update' exited." >&2
  echo "  The binary started the HTTP server in CLI mode." >&2
  exit 1
fi

echo "PASS: 14_cli_link_update"
