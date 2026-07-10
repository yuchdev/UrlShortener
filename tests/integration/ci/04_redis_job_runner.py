#!/usr/bin/env python3
import os, subprocess
if not os.getenv("REDIS_ADDRESS"):
    print("SKIP: REDIS_ADDRESS not set")
    raise SystemExit(0)
# redis__ and ratelimiter__ match C++ unit tests; with_redis matches C++ integration tests;
# the remaining basenames match Python integration tests registered by filename.
patterns = "|".join([
    "redis__",
    "ratelimiter__",
    "with_redis",
    "01_set_get_delete_ttl",
    "02_unavailable_redis_fail_open",
    "03_serialization_version_compatibility",
    "04_ratelimiter_fixed_window",
    "05_ratelimiter_concurrency",
    "storage__13_",
])
res = subprocess.run(["ctest", "-R", patterns, "--output-on-failure"], check=False)
raise SystemExit(res.returncode)
