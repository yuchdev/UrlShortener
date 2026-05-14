#!/usr/bin/env python3
import subprocess
jobs = [
    "test/integration/ci/01_inmemory_job_runner.py",
    "test/integration/ci/02_sqlite_job_runner.py",
    "test/integration/ci/03_postgres_job_runner.py",
    "test/integration/ci/04_redis_job_runner.py",
]
failed = []
for job in jobs:
    rc = subprocess.run(["python3", job], check=False).returncode
    print(f"{job}: {'PASS' if rc==0 else 'FAIL'}")
    if rc != 0:
        failed.append(job)
if failed:
    print("FAILED JOBS:")
    for j in failed: print(j)
    raise SystemExit(1)
print("Storage matrix complete")
