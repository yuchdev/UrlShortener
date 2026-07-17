#!/usr/bin/env python3
import subprocess
import os

ctest_args = ["ctest", "-R", "inmemory__|storage__0[1-6]_", "--output-on-failure"]
if os.getenv("URLSHORTENER_CTEST_CONFIG"):
    ctest_args[1:1] = ["-C", os.environ["URLSHORTENER_CTEST_CONFIG"]]

# Run only tests that exercise the in-memory adapter exclusively.
# The contract metadata tests (metadata__*) use MakeMetadataHarnesses(), which
# runs all enabled backends, so they are intentionally excluded here.
res = subprocess.run(ctest_args, check=False)
raise SystemExit(res.returncode)
