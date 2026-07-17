#!/usr/bin/env python3
"""Integration check that SOCI is fetched with both SQLite and PostgreSQL enabled."""

import os
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

cmake_args = ["cmake", "-S", str(probe_src), "-B", str(probe_build)]
if os.getenv("URLSHORTENER_CMAKE_TOOLCHAIN_FILE"):
    cmake_args.append(
        f"-DCMAKE_TOOLCHAIN_FILE={os.environ['URLSHORTENER_CMAKE_TOOLCHAIN_FILE']}"
    )
if os.getenv("URLSHORTENER_VCPKG_INSTALLED_DIR"):
    cmake_args.append(
        f"-DVCPKG_INSTALLED_DIR={os.environ['URLSHORTENER_VCPKG_INSTALLED_DIR']}"
    )
if os.getenv("URLSHORTENER_VCPKG_TARGET_TRIPLET"):
    cmake_args.append(
        f"-DVCPKG_TARGET_TRIPLET={os.environ['URLSHORTENER_VCPKG_TARGET_TRIPLET']}"
    )

configure = subprocess.run(
    cmake_args,
    capture_output=True,
    text=True,
)
assert configure.returncode == 0, configure.stderr

cache = (probe_build / "CMakeCache.txt").read_text()
assert "FETCHCONTENT_SOURCE_DIR_SOCI" in cache
assert any(token in cache for token in ("SOCI_SQLITE3:BOOL=ON", "SOCI_SQLITE3:STRING=ON", "WITH_SQLITE3:BOOL=ON", "WITH_SQLITE3:STRING=ON"))
assert any(token in cache for token in ("SOCI_POSTGRESQL:BOOL=ON", "SOCI_POSTGRESQL:STRING=ON", "WITH_POSTGRESQL:BOOL=ON", "WITH_POSTGRESQL:STRING=ON"))
assert any(token in cache for token in ("SOCI_MYSQL:BOOL=OFF", "SOCI_MYSQL:STRING=OFF", "WITH_MYSQL:BOOL=OFF", "WITH_MYSQL:STRING=OFF"))
assert any(token in cache for token in ("SOCI_ODBC:BOOL=OFF", "SOCI_ODBC:STRING=OFF", "WITH_ODBC:BOOL=OFF", "WITH_ODBC:STRING=OFF"))

print("ok")
