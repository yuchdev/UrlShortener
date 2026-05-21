#!/usr/bin/env python3
import os, subprocess
if not os.getenv("REDIS_ADDRESS"):
    print("SKIP: REDIS_ADDRESS not set")
    raise SystemExit(0)
res = subprocess.run(["ctest","-R","redis__|ratelimiter__","--output-on-failure"], check=False)
raise SystemExit(res.returncode)
