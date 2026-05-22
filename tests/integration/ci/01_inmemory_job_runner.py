#!/usr/bin/env python3
import subprocess
res = subprocess.run(["ctest","-L","contract","-R","metadata__0[1-8]_","--output-on-failure"], check=False)
raise SystemExit(res.returncode)
