#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
build = pathlib.Path(tempfile.mkdtemp(prefix="soci_postgres_enabled_"))

proc = subprocess.run(
    ["cmake", "-S", str(root), "-B", str(build), "-DBUILD_TESTING=OFF"],
    capture_output=True,
    text=True,
)
assert proc.returncode == 0, proc.stderr
cache = (build / "CMakeCache.txt").read_text()
assert "SOCI_POSTGRESQL:BOOL=ON" in cache or "WITH_POSTGRESQL:BOOL=ON" in cache
assert "PostgreSQL_INCLUDE_DIR:PATH=" in cache
assert "PostgreSQL_LIBRARY_RELEASE:FILEPATH=" in cache
print("ok")
