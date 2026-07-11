#!/usr/bin/env python3
import os, subprocess
if not os.getenv("URL_SHORTENER_TEST_POSTGRES_DSN"):
    print("SKIP: URL_SHORTENER_TEST_POSTGRES_DSN not set")
    raise SystemExit(0)
res = subprocess.run(["ctest","-R","psql__|postgres","--output-on-failure"], check=False)
raise SystemExit(res.returncode)
