#!/usr/bin/env bash
# tests/e2e/scripts/sections/16_cli_link_enable_disable.sh
#
# E2E CLI section 16: `link enable` and `link disable` must each exit promptly
# without ever binding a listening socket (plan.md C4; Task 03.0 subtask 03).
#
# Mirrors 13_cli_no_server_socket.sh's port-check pattern for both lifecycle
# verbs (they share SetLinkEnabled under the hood). A non-zero exit from either
# command is acceptable here (the slug may not exist in a fresh in-memory
# process); this section proves the lifetime/socket guarantee, not the
# success payload (Task 04.0).
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

# Run one verb one-shot and assert it exited without a lingering listener.
_run_verb_no_socket() {
  local verb="$1"

  if _port_open 8000; then
    echo "SKIP: port 8000 is already in use before '$verb'; skipping to avoid false failure."
    return 0
  fi

  if ! timeout 5 "$BINARY" link "$verb" \
         --slug e2e-lifecycle-slug \
         --base-domain http://sho.rt \
         >/dev/null 2>&1; then
    local rc=$?
    if [[ "$rc" -eq 124 ]]; then
      echo "FAIL: 'link $verb' did not exit within 5s (lifetime guarantee)." >&2
      exit 1
    fi
  fi

  sleep 0.3

  if _port_open 8000; then
    echo "FAIL: port 8000 is OPEN after 'link $verb' exited." >&2
    echo "  The binary started the HTTP server in CLI mode." >&2
    exit 1
  fi
}

echo "=== 16_cli_link_enable_disable ==="

_run_verb_no_socket enable
_run_verb_no_socket disable

echo "PASS: 16_cli_link_enable_disable"
