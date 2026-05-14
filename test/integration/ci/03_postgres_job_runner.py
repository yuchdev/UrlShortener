#!/usr/bin/env python3
import os, subprocess
if not os.getenv("POSTGRES_DSN"):
    print("SKIP: POSTGRES_DSN not set")
    raise SystemExit(0)
res = subprocess.run(["ctest","-R","psql__|postgres","--output-on-failure"], check=False)
raise SystemExit(res.returncode)
