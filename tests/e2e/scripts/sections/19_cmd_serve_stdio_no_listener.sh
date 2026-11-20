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
set -euo pipefail

# Shared helpers (run_with_timeout: portable `timeout`/`gtimeout`/watchdog).
# Defined once in common.sh (subtask 33); this section no longer carries a
# local copy.
# shellcheck source=tests/e2e/scripts/common.sh
source "$(dirname "$0")/../common.sh"

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

# This section depends on python3 for the loopback connect probe and for the
# byte+mtime fingerprint of the sentinel uri.txt. Guard it explicitly so a host
# without python3 fails with a clear message rather than a confusing error.
if ! command -v python3 >/dev/null 2>&1; then
  echo "ERROR: python3 is required for 19_cmd_serve_stdio_no_listener." >&2
  exit 2
fi

TMPDIR_19="$(mktemp -d)"
trap 'rm -rf "$TMPDIR_19"' EXIT

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

# Positive control: can lsof actually enumerate a known listening socket here?
# Opens a throwaway python TCP listener and checks lsof reports it for that pid,
# so an lsof that silently enumerates nothing (sandbox) is not mistaken for
# "no listener". Prints "ok" on success. Bounded by the short-lived child.
_lsof_can_enumerate() {
  python3 - <<'PY'
import os, socket, subprocess, sys
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.bind(("127.0.0.1", 0)); s.listen(1)
port = s.getsockname()[1]
try:
    out = subprocess.run(
        ["lsof", "-nP", "-a", "-p", str(os.getpid()), "-iTCP", "-sTCP:LISTEN"],
        capture_output=True, text=True, timeout=10).stdout
finally:
    s.close()
print("ok" if f":{port}" in out and "(LISTEN)" in out else "no")
PY
}

# Returns 0 when PID owns a listening TCP socket OR any UNIX-domain socket (a
# failure), 1 when it owns none. Trusts lsof only after the positive control;
# also checks UNIX-domain sockets (invisible to a TCP probe). Uses ss on Linux,
# and falls back to a port-8000 connect probe (no PID attribution) otherwise.
_pid_listens() {
  local pid="$1"
  if command -v lsof >/dev/null 2>&1 && [[ "$(_lsof_can_enumerate)" == "ok" ]]; then
    if lsof -nP -a -p "$pid" -iTCP -sTCP:LISTEN 2>/dev/null | grep -q "(LISTEN)"; then
      return 0
    fi
    # UNIX-domain sockets: any data line (past the header) is a socket the PID
    # owns; the local modes open none.
    if lsof -nP -U -a -p "$pid" 2>/dev/null | tail -n +2 | grep -q .; then
      return 0
    fi
    return 1
  fi
  if command -v ss >/dev/null 2>&1; then
    ss -ltnp 2>/dev/null | grep -q "pid=${pid}," && return 0
    ss -lxp 2>/dev/null | grep -q "pid=${pid}," && return 0
    return 1
  fi
  echo "  NOTE: no working lsof/ss; falling back to a port-8000 connect probe (no PID attribution)."
  if _port_open 8000; then
    return 0
  fi
  return 1
}

# Byte+mtime fingerprint of a file (portable via python3): proves the sentinel
# was neither rewritten nor touched. `cksum` alone would miss a same-content
# rewrite that still bumps mtime.
_file_fingerprint() {
  python3 - "$1" <<'PY'
import hashlib, os, sys
p = sys.argv[1]
with open(p, "rb") as handle:
    digest = hashlib.sha256(handle.read()).hexdigest()
print(f"{digest}:{os.stat(p).st_mtime_ns}")
PY
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
  before="$(_file_fingerprint "$workdir/uri.txt")"
  if [[ "$mode" == "cmd" ]]; then
    ( cd "$workdir" && run_with_timeout 10 "$BINARY" cmd command.list ) \
      >/dev/null 2>&1 || true
  else
    ( cd "$workdir" \
        && printf '%s\n' '{"v":1,"command":"command.list"}' \
           | run_with_timeout 10 "$BINARY" serve-stdio ) \
      >/dev/null 2>&1 || true
  fi
  after="$(_file_fingerprint "$workdir/uri.txt")"
  if [[ "$before" != "$after" ]]; then
    echo "FAIL: $mode changed the sentinel uri.txt bytes or mtime (URI store written)." >&2
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
