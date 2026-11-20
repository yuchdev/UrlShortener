#!/usr/bin/env bash
# tests/e2e/scripts/sections/19_cmd_serve_stdio_no_listener.sh
#
# E2E CLI section 19: the unified local modes (`cmd`, `serve-stdio`, `tui`) must
# never bind a listening socket and never create or read the legacy `uri.txt`
# URI-store (Milestone 0006, plan.md contract C5; Task 07.0 subtask 21).
#
# This extends section 13 (`link` no-listener) to the three new entry points and
# proves three things with plain POSIX tooling (mirroring 13's style):
#   A) No listener: `cmd` exits without leaving port 8000 open; while a
#      `serve-stdio` process is alive it owns no LISTEN socket (lsof/ss, with a
#      port-probe fallback); a non-TTY `tui` exits 2 and leaves no listener.
#   B) No URI store: a sentinel `uri.txt` in the working directory is
#      byte-identical after `cmd` / `serve-stdio` run there.
#   C) `--port 9999` is rejected or ignored: port 9999 never listens.
#
# PID socket ownership is platform-specific (lsof on macOS, ss on Linux) with a
# bounded loopback connect probe as the documented fallback. Every wait is
# bounded via run_with_timeout (prefer `timeout`, then `gtimeout`, else a
# pure-shell watchdog) - no wall-clock assertions.
#
# NOTE for subtask 33: this script defines `run_with_timeout` locally because
# no shared helper exists in tests/e2e/scripts/common.sh yet. When subtask 33
# adds a shared `run_with_timeout` to common.sh, this local copy should be
# replaced by sourcing it.
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

TMPDIR_19="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_19"' EXIT

# Bounded command runner: prefer GNU `timeout`, then `gtimeout` (macOS/MacPorts),
# else a pure-shell watchdog. Returns the child's exit code, or 124 when the
# watchdog had to terminate a hung child (matching `timeout`'s convention).
run_with_timeout() {
  local secs="$1"
  shift
  if command -v timeout >/dev/null 2>&1; then
    timeout "$secs" "$@"
    return $?
  fi
  if command -v gtimeout >/dev/null 2>&1; then
    gtimeout "$secs" "$@"
    return $?
  fi
  # Pure-shell fallback watchdog.
  "$@" &
  local cmd_pid=$!
  (
    sleep "$secs"
    kill -TERM "$cmd_pid" 2>/dev/null || true
  ) &
  local wd_pid=$!
  local rc=0
  if wait "$cmd_pid" 2>/dev/null; then
    rc=0
  else
    rc=$?
  fi
  # If the watchdog is still alive, the child finished first: cancel it. If it
  # already exited, the child was killed (hang) -> normalize to 124.
  if kill -0 "$wd_pid" 2>/dev/null; then
    kill "$wd_pid" 2>/dev/null || true
    wait "$wd_pid" 2>/dev/null || true
  else
    rc=124
  fi
  return $rc
}

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

# Returns 0 when PID owns a LISTEN socket (a failure), 1 when it owns none.
# Uses lsof (macOS/Linux) or ss (Linux); falls back to a port-8000 connect probe
# (no PID attribution) when neither tool is present.
_pid_listens() {
  local pid="$1"
  if command -v lsof >/dev/null 2>&1; then
    lsof -nP -a -p "$pid" -iTCP -sTCP:LISTEN 2>/dev/null | grep -q "(LISTEN)"
    return $?
  fi
  if command -v ss >/dev/null 2>&1; then
    ss -ltnp 2>/dev/null | grep -q "pid=${pid},"
    return $?
  fi
  echo "  NOTE: no lsof/ss; falling back to a port-8000 connect probe (no PID attribution)."
  if _port_open 8000; then
    return 0
  fi
  return 1
}

echo "=== 19_cmd_serve_stdio_no_listener ==="

# Pre-check: skip if port 8000 is already in use (conflict with another process).
if _port_open 8000; then
  echo "SKIP: port 8000 is already in use before the test; skipping to avoid false failure."
  exit 0
fi

# ---------------------------------------------------------------------------
# Check A1: `cmd` exits and leaves no listener on port 8000.
# ---------------------------------------------------------------------------
rc=0
run_with_timeout 10 "$BINARY" cmd command.list >/dev/null 2>&1 || rc=$?
if [[ "$rc" -eq 124 ]]; then
  echo "FAIL: 'cmd command.list' did not exit within budget (listener/hang)." >&2
  exit 1
fi
sleep 0.2
if _port_open 8000; then
  echo "FAIL: port 8000 is OPEN after 'cmd command.list' exited." >&2
  exit 1
fi
echo "PASS A1: cmd exits with no listener"

# ---------------------------------------------------------------------------
# Check A2: a live `serve-stdio` process owns no LISTEN socket.
# A FIFO keeps the child's stdin open so it stays alive long enough to sample
# its PID; closing the write fd sends EOF and the child exits.
# ---------------------------------------------------------------------------
FIFO="$TMPDIR_19/stdio_in"
mkfifo "$FIFO"
"$BINARY" serve-stdio < "$FIFO" > "$TMPDIR_19/stdio_out" 2>/dev/null &
stdio_pid=$!
# Open the write end and hold it so the reader does not see EOF yet.
exec 9>"$FIFO"
printf '%s\n' '{"v":1,"command":"command.list"}' >&9
sleep 0.3
if ! kill -0 "$stdio_pid" 2>/dev/null; then
  exec 9>&-
  echo "FAIL: serve-stdio exited before it could be sampled." >&2
  exit 1
fi
listen_rc=0
_pid_listens "$stdio_pid" || listen_rc=$?
# Close stdin (EOF) and reap before asserting, so the child never lingers.
exec 9>&-
wait "$stdio_pid" 2>/dev/null || true
if [[ "$listen_rc" -eq 0 ]]; then
  echo "FAIL: serve-stdio (pid $stdio_pid) owns a LISTEN socket (no listener allowed)." >&2
  exit 1
fi
echo "PASS A2: serve-stdio owns no listener while running"

# ---------------------------------------------------------------------------
# Check A3: a non-interactive `tui` exits 2 and leaves no listener.
# ---------------------------------------------------------------------------
rc=0
run_with_timeout 10 "$BINARY" tui </dev/null >/dev/null 2>&1 || rc=$?
if [[ "$rc" -eq 124 ]]; then
  echo "FAIL: non-interactive 'tui' did not exit within budget." >&2
  exit 1
fi
if [[ "$rc" -ne 2 ]]; then
  echo "FAIL: non-interactive 'tui' exited $rc, expected 2 (non-TTY refusal)." >&2
  exit 1
fi
sleep 0.2
if _port_open 8000; then
  echo "FAIL: port 8000 is OPEN after 'tui' refused a non-TTY stdin." >&2
  exit 1
fi
echo "PASS A3: non-interactive tui exits 2 with no listener"

# ---------------------------------------------------------------------------
# Check B: a sentinel uri.txt is byte-identical after cmd / serve-stdio run in
# that working directory (the legacy URI store is never created or written).
# ---------------------------------------------------------------------------
SENTINEL_MARKER="SENTINEL-19-URI-DO-NOT-WRITE-7a6b5c"
for mode in cmd serve-stdio; do
  workdir="$TMPDIR_19/cwd_${mode}"
  mkdir -p "$workdir"
  printf '%s short->http://example.com/\n' "$SENTINEL_MARKER" > "$workdir/uri.txt"
  before="$(cksum < "$workdir/uri.txt")"
  if [[ "$mode" == "cmd" ]]; then
    ( cd "$workdir" && run_with_timeout 10 "$BINARY" cmd command.list ) \
      >/dev/null 2>&1 || true
  else
    ( cd "$workdir" \
        && printf '%s\n' '{"v":1,"command":"command.list"}' \
           | run_with_timeout 10 "$BINARY" serve-stdio ) \
      >/dev/null 2>&1 || true
  fi
  after="$(cksum < "$workdir/uri.txt")"
  if [[ "$before" != "$after" ]]; then
    echo "FAIL: $mode modified the sentinel uri.txt (URI store was written)." >&2
    exit 1
  fi
  # The sentinel must remain the only uri*.txt file in the cwd.
  extra="$(find "$workdir" -maxdepth 1 -name 'uri*.txt' ! -name 'uri.txt' | wc -l | tr -d '[:space:]')"
  if [[ "$extra" != "0" ]]; then
    echo "FAIL: $mode created an unexpected uri*.txt file." >&2
    exit 1
  fi
done
echo "PASS B: sentinel uri.txt unchanged after cmd and serve-stdio"

# ---------------------------------------------------------------------------
# Check C: `--port 9999` is rejected or ignored; port 9999 never listens.
# ---------------------------------------------------------------------------
if _port_open 9999; then
  echo "SKIP C: port 9999 already in use before the test."
else
  rc=0
  run_with_timeout 10 "$BINARY" cmd --port 9999 command.list >/dev/null 2>&1 || rc=$?
  if [[ "$rc" -eq 124 ]]; then
    echo "FAIL: 'cmd --port 9999' did not exit within budget." >&2
    exit 1
  fi
  sleep 0.2
  if _port_open 9999; then
    echo "FAIL: port 9999 is OPEN - 'cmd --port 9999' started a server." >&2
    exit 1
  fi
  echo "PASS C: --port 9999 does not open a listener"
fi

echo "PASS: 19_cmd_serve_stdio_no_listener"
