#!/usr/bin/env python3
"""Integration check that SOCI is fetched with both SQLite and PostgreSQL enabled."""

import pathlib
import subprocess
import tempfile

repo_root = pathlib.Path(__file__).resolve().parents[3]
probe_root = pathlib.Path(tempfile.mkdtemp(prefix="soci_fetch_probe_"))
probe_src = probe_root / "src"
probe_build = probe_root / "build"
probe_src.mkdir(parents=True, exist_ok=True)

(probe_src / "CMakeLists.txt").write_text(
    f"""
cmake_minimum_required(VERSION 3.20)
project(soci_fetch_probe LANGUAGES CXX)
include(\"{(repo_root / 'cmake' / 'FetchSOCI.cmake').as_posix()}\")
fetch_soci_sqlite_and_postgres()
""".strip()
    + "\n"
)

configure = subprocess.run(
    ["cmake", "-S", str(probe_src), "-B", str(probe_build)],
    capture_output=True,
    text=True,
)
assert configure.returncode == 0, configure.stderr

cache = (probe_build / "CMakeCache.txt").read_text()
assert "FETCHCONTENT_SOURCE_DIR_SOCI" in cache
assert "SOCI_SQLITE3:BOOL=ON" in cache or "WITH_SQLITE3:BOOL=ON" in cache
assert "SOCI_POSTGRESQL:BOOL=ON" in cache or "WITH_POSTGRESQL:BOOL=ON" in cache
assert "SOCI_MYSQL:BOOL=OFF" in cache or "WITH_MYSQL:BOOL=OFF" in cache
assert "SOCI_ODBC:BOOL=OFF" in cache or "WITH_ODBC:BOOL=OFF" in cache

print("ok")
