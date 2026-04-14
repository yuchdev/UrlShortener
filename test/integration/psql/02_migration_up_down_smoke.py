#!/usr/bin/env python3
import pathlib

root = pathlib.Path(__file__).resolve().parents[3]
up = root / "db" / "migrations" / "postgres" / "001_create_links.up.sql"
down = root / "db" / "migrations" / "postgres" / "001_create_links.down.sql"

assert up.exists()
assert down.exists()
up_sql = up.read_text()
down_sql = down.read_text()
assert "CREATE TABLE" in up_sql and "JSONB" in up_sql
assert "DROP TABLE" in down_sql
print("ok")
