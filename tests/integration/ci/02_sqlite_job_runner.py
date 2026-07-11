#!/usr/bin/env python3
import subprocess

# Only run sqlite-relevant tests; explicitly exclude this runner to avoid recursion/timeouts.
res = subprocess.run([
    "ctest",
    "-R",
    "sqlite__|storage__07_sqlite_service_full_flow|01_fetch_soci_sqlite_only|02_sqlite_backend_enabled|03_fetch_soci_sqlite_and_postgres|01_db_file_initialization|02_busy_lock_behavior",
    "-E",
    "02_sqlite_job_runner|05_full_storage_matrix",
    "--output-on-failure",
], check=False)
raise SystemExit(res.returncode)
