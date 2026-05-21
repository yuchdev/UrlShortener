#!/usr/bin/env python3
import pathlib

root = pathlib.Path(__file__).resolve().parents[3]
config = (root / "include" / "url_shortener" / "storage" / "sql" / "SqlConnectionConfig.hpp").read_text()
factory = (root / "src" / "storage" / "postgres" / "PostgresSessionFactory.cpp").read_text()

assert "max_retries" in config
assert "RunWithRetry" in factory
print("ok")
