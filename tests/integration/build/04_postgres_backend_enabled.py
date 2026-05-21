#!/usr/bin/env python3
"""Integration check that SOCI PostgreSQL backend is enabled and discoverable."""

import pathlib
import subprocess
import tempfile

repo_root = pathlib.Path(__file__).resolve().parents[3]
probe_root = pathlib.Path(tempfile.mkdtemp(prefix="soci_postgres_probe_"))
probe_src = probe_root / "src"
probe_build = probe_root / "build"
probe_src.mkdir(parents=True, exist_ok=True)

(probe_src / "CMakeLists.txt").write_text(
    f"""
cmake_minimum_required(VERSION 3.20)
project(soci_postgres_probe LANGUAGES CXX)
include(\"{(repo_root / 'cmake' / 'FetchSOCI.cmake').as_posix()}\")
fetch_soci_sqlite_and_postgres()
""".strip()
    + "\n"
)

proc = subprocess.run(
    ["cmake", "-S", str(probe_src), "-B", str(probe_build)],
    capture_output=True,
    text=True,
)
assert proc.returncode == 0, proc.stderr
cache = (probe_build / "CMakeCache.txt").read_text()
assert "SOCI_POSTGRESQL:BOOL=ON" in cache or "WITH_POSTGRESQL:BOOL=ON" in cache
assert "PostgreSQL_INCLUDE_DIR:PATH=" in cache
assert "PostgreSQL_LIBRARY_RELEASE:FILEPATH=" in cache
print("ok")
