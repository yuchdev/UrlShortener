#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parent
jobs = [
    root / "01_inmemory_job_runner.py",
    root / "02_sqlite_job_runner.py",
    root / "03_postgres_job_runner.py",
    root / "04_redis_job_runner.py",
]
failed = []
for job in jobs:
    rc = subprocess.run([sys.executable, str(job)], check=False).returncode
    print(f"{job.name}: {'PASS' if rc==0 else 'FAIL'}")
    if rc != 0:
        failed.append(job.name)
if failed:
    print("FAILED JOBS:")
    for job_name in failed:
        print(job_name)
    raise SystemExit(1)
print("Storage matrix complete")
