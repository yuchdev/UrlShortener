#!/usr/bin/env python3
import subprocess
res = subprocess.run(["ctest","-R","sqlite__|sqlite_","--output-on-failure"], check=False)
raise SystemExit(res.returncode)
