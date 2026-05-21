#!/usr/bin/env python3
import os
import subprocess


dsn = os.getenv("URL_SHORTENER_TEST_POSTGRES_DSN", "")
if not dsn:
    print("skip: URL_SHORTENER_TEST_POSTGRES_DSN not set")
    raise SystemExit(0)

proc = subprocess.run(["psql", dsn, "-c", "SELECT 1"], capture_output=True, text=True)
assert proc.returncode == 0, proc.stderr
print("ok")
