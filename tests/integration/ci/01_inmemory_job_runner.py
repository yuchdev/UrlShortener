#!/usr/bin/env python3
import subprocess
# Run only tests that exercise the in-memory adapter exclusively.
# The contract metadata tests (metadata__*) use MakeMetadataHarnesses(), which
# runs all enabled backends, so they are intentionally excluded here.
res = subprocess.run(["ctest", "-R", "inmemory__|storage__0[1-6]_", "--output-on-failure"], check=False)
raise SystemExit(res.returncode)
