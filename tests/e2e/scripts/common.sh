#!/usr/bin/env bash
set -euo pipefail

DB_PATH="tests/e2e/tmp/qa.sqlite"
LOG_PATH="tests/e2e/tmp/server.log"
PID_PATH="tests/e2e/tmp/mock_service.pid"

mkdir -p tests/e2e/tmp tests/e2e/failures

# run_with_timeout <seconds> <cmd...> : bound a command's runtime portably.
# Prefers GNU `timeout`, then `gtimeout` (macOS/MacPorts); where neither is on
# PATH it falls back to a pure-shell watchdog. Preserves the command's own exit
# status and returns 124 on timeout, matching GNU `timeout` so the sections'
# existing "capture rc with || rc=$?" logic (and the `[[ rc -eq 124 ]]` checks)
# is unchanged regardless of which path is taken.
#
# Fallback-watchdog hardening (only when no timeout/gtimeout is present):
#   * The command runs in its own process group - via `setsid` where available
#     (Linux), else bash monitor mode (`set -m`, e.g. macOS) - so the watchdog
#     signals the whole group ("-PGID"): grandchildren are reaped and a recycled
#     PID can never be hit. Where neither is available it signals the lone PID.
#   * On timeout the watchdog sends SIGTERM, waits a short grace period (~2 s),
#     then escalates to SIGKILL, so the helper always returns even if the child
#     ignores SIGTERM.
#   * A timeout is reported (rc 124) only when the watchdog actually fired,
#     recorded via a marker file - never inferred from process timing - so the
#     boundary false-timeout race is gone.
#   * The watchdog polls a done-flag and exits on its own once the command
#     finishes, so it never leaves an orphaned sleep behind.
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

  # Pure-shell fallback watchdog (no timeout/gtimeout on PATH).
  local state_dir marker done_flag use_group had_m
  state_dir="$(mktemp -d)"
  marker="$state_dir/fired"
  done_flag="$state_dir/done"
  use_group=0
  if command -v setsid >/dev/null 2>&1; then
    use_group=1
    setsid "$@" &
  else
    had_m=0
    case "$-" in *m*) had_m=1 ;; esac
    if set -m 2>/dev/null; then
      use_group=1
    fi
    "$@" &
  fi
  local cmd_pid=$!
  # Restore the previous monitor-mode setting if we enabled it above.
  if [ "${had_m:-1}" -eq 0 ]; then
    set +m 2>/dev/null || true
  fi
  # With setsid or `set -m` the child leads its own group (PGID == PID); signal
  # the negated PID to reach the whole group.
  local target="$cmd_pid"
  [ "$use_group" -eq 1 ] && target="-$cmd_pid"

  (
    # Wait up to <secs>, polling so the watchdog exits promptly - and without
    # orphaning a long sleep - as soon as the command signals completion.
    steps=$(( secs * 4 ))
    while [ "$steps" -gt 0 ]; do
      [ -e "$done_flag" ] && exit 0
      sleep 0.25
      steps=$(( steps - 1 ))
    done
    [ -e "$done_flag" ] && exit 0
    : > "$marker"
    kill -TERM "$target" 2>/dev/null || true
    # Grace period (~2 s), then escalate to SIGKILL so the helper always returns.
    grace=8
    while [ "$grace" -gt 0 ]; do
      [ -e "$done_flag" ] && exit 0
      sleep 0.25
      grace=$(( grace - 1 ))
    done
    kill -KILL "$target" 2>/dev/null || true
  ) &
  local wd_pid=$!

  local rc=0
  if wait "$cmd_pid" 2>/dev/null; then
    rc=0
  else
    rc=$?
  fi
  # Tell the watchdog to stop; it exits at its next poll (no orphaned sleep).
  : > "$done_flag"
  wait "$wd_pid" 2>/dev/null || true

  # Report a timeout only when the watchdog actually fired.
  [ -e "$marker" ] && rc=124
  rm -rf "$state_dir"
  return $rc
}

init_db() {
  python3 - <<'PY'
import sqlite3
from pathlib import Path

db = Path("tests/e2e/tmp/qa.sqlite")
db.parent.mkdir(parents=True, exist_ok=True)
conn = sqlite3.connect(str(db))
conn.executescript(
    """
CREATE TABLE IF NOT EXISTS app_users(id TEXT PRIMARY KEY, username TEXT);
CREATE TABLE IF NOT EXISTS links(short_code TEXT PRIMARY KEY, target_url TEXT, status TEXT);
CREATE TABLE IF NOT EXISTS click_events(id TEXT PRIMARY KEY, short_code TEXT, ip TEXT);
CREATE TABLE IF NOT EXISTS fingerprints(id TEXT PRIMARY KEY, fingerprint TEXT, suspicious INTEGER);
CREATE TABLE IF NOT EXISTS auth_sessions(id TEXT PRIMARY KEY, user_id TEXT, expires_at INTEGER);
"""
)
conn.commit()
conn.close()
PY
}

reset_db() {
  python3 -m tools.sqlite_state_assert --db "$DB_PATH" --reset-database >/dev/null
}

start_service() {
  : > "$LOG_PATH"
  python3 tests/e2e/scripts/mock_service.py > tests/e2e/tmp/service.stdout 2> tests/e2e/tmp/service.stderr &
  echo $! > "$PID_PATH"
  python3 -m tools.sqlite_state_assert --assert-port-open 18080 >/dev/null
}

stop_service() {
  if [[ -f "$PID_PATH" ]]; then
    python3 - <<'PY'
from pathlib import Path
import os
import signal

pid_path = Path("tests/e2e/tmp/mock_service.pid")
if pid_path.exists():
    pid = int(pid_path.read_text(encoding="utf-8").strip())
    try:
        os.kill(pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    pid_path.unlink(missing_ok=True)
PY
  fi
  python3 -m tools.sqlite_state_assert --assert-port-closed 18080 >/dev/null || true
}

cleanup() {
  stop_service
  reset_db || true
}
