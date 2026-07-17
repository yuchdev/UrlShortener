#!/usr/bin/env python3
import subprocess
import os

ctest_args = [
    "ctest",
    "-R",
    "sqlite__|storage__07_sqlite_service_full_flow|01_fetch_soci_sqlite_only|02_sqlite_backend_enabled|03_fetch_soci_sqlite_and_postgres|01_db_file_initialization|02_busy_lock_behavior",
    "-E",
    "02_sqlite_job_runner|05_full_storage_matrix",
    "--output-on-failure",
]
if os.getenv("URLSHORTENER_CTEST_CONFIG"):
    ctest_args[1:1] = ["-C", os.environ["URLSHORTENER_CTEST_CONFIG"]]

# Only run sqlite-relevant tests; explicitly exclude this runner to avoid recursion/timeouts.
res = subprocess.run(ctest_args, check=False)
raise SystemExit(res.returncode)
