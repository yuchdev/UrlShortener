#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[3]
build = pathlib.Path(tempfile.mkdtemp(prefix="soci_sql_backends_on_"))

proc = subprocess.run(
    ["cmake", "-S", str(root), "-B", str(build), "-DBUILD_TESTING=OFF"],
    capture_output=True,
    text=True,
)
assert proc.returncode == 0, proc.stderr
cache = (build / "CMakeCache.txt").read_text()
assert any(token in cache for token in ("SOCI_SQLITE3:BOOL=ON", "SOCI_SQLITE3:STRING=ON", "WITH_SQLITE3:BOOL=ON", "WITH_SQLITE3:STRING=ON"))
assert any(token in cache for token in ("SOCI_POSTGRESQL:BOOL=ON", "SOCI_POSTGRESQL:STRING=ON", "WITH_POSTGRESQL:BOOL=ON", "WITH_POSTGRESQL:STRING=ON"))
print("ok")
