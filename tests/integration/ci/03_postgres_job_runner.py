#!/usr/bin/env python3
import os, subprocess
if not os.getenv("URL_SHORTENER_TEST_POSTGRES_DSN"):
    print("SKIP: URL_SHORTENER_TEST_POSTGRES_DSN not set")
    raise SystemExit(0)
ctest_args = ["ctest", "-R", "psql__|postgres", "--output-on-failure"]
if os.getenv("URLSHORTENER_CTEST_CONFIG"):
    ctest_args[1:1] = ["-C", os.environ["URLSHORTENER_CTEST_CONFIG"]]
res = subprocess.run(ctest_args, check=False)
raise SystemExit(res.returncode)
