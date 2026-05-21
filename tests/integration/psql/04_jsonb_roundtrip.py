#!/usr/bin/env python3
import pathlib

root = pathlib.Path(__file__).resolve().parents[3]
dialect = (root / "src" / "storage" / "postgres" / "PostgresSqlDialect.cpp").read_text()

assert "JSONB" in dialect
assert "attributes_json" in dialect
print("ok")
