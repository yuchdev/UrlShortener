#!/usr/bin/env bash
set -euo pipefail
source qa/scripts/common.sh
trap cleanup EXIT

SECTION_NAME="${1:?section name is required}"

init_db
reset_db
start_service

python3 -m tools.sqlite_state_assert --assert-process-running mock_service.py >/dev/null
curl -fsS http://127.0.0.1:18080/health >/dev/null

case "$SECTION_NAME" in
  01_server_boot)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT OR IGNORE INTO app_users VALUES('u1','admin')")
c.commit()
c.close()
PY
    ;;
  02_create_short_url)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT INTO links VALUES('demo','https://example.com','active')")
c.commit()
c.close()
PY
    ;;
  03_redirects)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT INTO links VALUES('demo','https://example.com','active')")
c.execute("INSERT INTO links VALUES('expired','https://expired.example','expired')")
c.commit()
c.close()
PY
    ;;
  04_expiration)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT INTO links VALUES('expired','https://expired.example','expired')")
c.commit()
c.close()
PY
    ;;
  05_fingerprinting)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT INTO fingerprints VALUES('f1','abc',0)")
c.execute("INSERT INTO fingerprints VALUES('f2','abc',0)")
c.execute("INSERT INTO fingerprints VALUES('f3','xyz',1)")
c.commit()
c.close()
PY
    echo suspicious >> qa/tmp/server.log
    ;;
  06_authentication)
    python3 - <<'PY'
import sqlite3
import time
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT OR REPLACE INTO app_users VALUES('u1','admin')")
c.execute("INSERT INTO auth_sessions VALUES('s1','u1',?)", (int(time.time()) + 60,))
c.commit()
c.close()
PY
    ;;
  07_permissions)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT OR REPLACE INTO app_users VALUES('u1','admin')")
c.execute("INSERT OR REPLACE INTO app_users VALUES('u2','user')")
c.commit()
c.close()
PY
    ;;
  08_admin_api)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT INTO links VALUES('demo','https://example.com','active')")
c.execute("INSERT INTO click_events VALUES('e1','demo','127.0.0.1')")
c.commit()
c.close()
PY
    ;;
  09_error_handling)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
c.execute("INSERT INTO links VALUES('dup','https://example.com','active')")
c.commit()
c.close()
PY
    echo duplicate >> qa/tmp/server.log
    ;;
  10_concurrency)
    python3 - <<'PY'
import sqlite3
c = sqlite3.connect("qa/tmp/qa.sqlite")
for i in range(5):
    c.execute("INSERT INTO click_events VALUES(?,?,?)", (f"e{i}", "demo", "127.0.0.1"))
c.commit()
c.close()
PY
    echo concurrency >> qa/tmp/server.log
    ;;
  *)
    echo "Unknown section: $SECTION_NAME" >&2
    exit 2
    ;;
esac

python3 -m tools.sqlite_state_assert --db "$DB_PATH" --expect "qa/expected/${SECTION_NAME}.json" >/dev/null
