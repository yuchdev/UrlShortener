#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
build = pathlib.Path(tempfile.mkdtemp(prefix="soci_sqlite_on_"))

proc = subprocess.run(
    ["cmake", "-S", str(root), "-B", str(build), "-DBUILD_TESTING=OFF"],
    capture_output=True,
    text=True,
)
assert proc.returncode == 0, proc.stderr
cache = (build / "CMakeCache.txt").read_text()
assert "SOCI_SQLITE3:BOOL=ON" in cache or "WITH_SQLITE3:BOOL=ON" in cache
assert "SOCI_POSTGRESQL:BOOL=OFF" in cache or "WITH_POSTGRESQL:BOOL=OFF" in cache
print("ok")
