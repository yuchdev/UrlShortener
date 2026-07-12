#!/usr/bin/env bash
set -euo pipefail

DB_PATH="tests/e2e/tmp/qa.sqlite"
LOG_PATH="tests/e2e/tmp/server.log"
PID_PATH="tests/e2e/tmp/mock_service.pid"

mkdir -p tests/e2e/tmp tests/e2e/failures

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
