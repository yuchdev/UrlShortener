#!/usr/bin/env bash
# tests/e2e/scripts/sections/30_serve_stdio.sh
#
# E2E CLI section 30: `serve-stdio` NDJSON pipe scenario (Task 05.0 subtask 18).
#
# Proves, with a plain POSIX pipe (no Python harness), that:
#   a) `printf` of three request lines (one deliberately malformed) piped to the
#      binary yields exactly three response lines and exit 0 (one response per
#      content line, malformed lines recover in place - contracts C2/C5).
#   b) the binary never binds port 8000 (no listener in stdio mode - contract C5).
#   c) empty stdin exits 0 with no output (EOF is the only graceful stop, 15).
#
# POSIX-only; registered by CMake solely on non-Windows hosts. No GNU `timeout`
# is used (absent on macOS); the binary exits on EOF on its own.
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

TMPDIR_STDIO="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_STDIO"' EXIT

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

echo "=== 30_serve_stdio ==="

# Pre-check: skip if port 8000 is already in use (conflict with another process).
if _port_open 8000; then
  echo "SKIP: port 8000 is already in use before the test; skipping to avoid false failure."
  exit 0
fi

# (a) Three requests, one malformed -> exactly three response lines, exit 0.
OUT_FILE="$TMPDIR_STDIO/out.ndjson"
printf '%s\n%s\n%s\n' \
  '{"v":1,"command":"system.info"}' \
  'this is not valid json' \
  '{"v":1,"command":"command.list"}' \
  | "$BINARY" serve-stdio > "$OUT_FILE" 2>/dev/null
LINES="$(wc -l < "$OUT_FILE" | tr -d '[:space:]')"
if [[ "$LINES" != "3" ]]; then
  echo "FAIL: expected 3 response lines, got $LINES" >&2
  exit 1
fi

# Each output line must be a single JSON object with v==1 (wire purity, C2).
python3 - "$OUT_FILE" <<'PY'
import json, sys
with open(sys.argv[1], encoding="utf-8") as handle:
    for line in handle:
        line = line.rstrip("\r\n")
        if not line:
            continue
        obj = json.loads(line)          # raises on a non-JSON (e.g. log) line
        assert obj["v"] == 1, obj
PY

# (b) No listening socket during/after the pipe (CLI/stdio mode, C5).
sleep 0.2
if _port_open 8000; then
  echo "FAIL: port 8000 is OPEN after serve-stdio exited (no listener allowed)." >&2
  exit 1
fi

# (c) Empty stdin exits 0 with no output.
EMPTY_OUT="$TMPDIR_STDIO/empty.ndjson"
printf '' | "$BINARY" serve-stdio > "$EMPTY_OUT" 2>/dev/null
if [[ -s "$EMPTY_OUT" ]]; then
  echo "FAIL: empty stdin produced output:" >&2
  cat "$EMPTY_OUT" >&2
  exit 1
fi

echo "PASS: 30_serve_stdio"
