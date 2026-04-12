#!/usr/bin/env python3
import sqlite3
import tempfile
from pathlib import Path

path = Path(tempfile.gettempdir()) / "url_shortener_init_check.sqlite3"
if path.exists():
    path.unlink()
conn = sqlite3.connect(path)
conn.execute("CREATE TABLE IF NOT EXISTS links(short_code TEXT PRIMARY KEY, target_url TEXT NOT NULL, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL, expires_at INTEGER NULL, is_active INTEGER NOT NULL, owner_id TEXT NULL, attributes_json TEXT NULL)")
conn.commit()
row = conn.execute("SELECT name FROM sqlite_master WHERE name='links'").fetchone()
assert row is not None
print("ok")
