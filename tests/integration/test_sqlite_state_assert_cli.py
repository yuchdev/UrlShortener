"""Integration tests for the sqlite_state_assert CLI tool."""
import json
import sqlite3
import subprocess
import sys
from pathlib import Path

import pytest

# Path to the fixture directory (relative to this file)
FIXTURES_DIR = Path(__file__).parent / "fixtures" / "sqlite_state"


def _apply_sql_file(conn: sqlite3.Connection, sql_path: Path) -> None:
    """Execute each statement from a SQL file on the given connection."""
    sql = sql_path.read_text(encoding="utf-8")
    for stmt in sql.split(";"):
        stmt = stmt.strip()
        if stmt:
            conn.execute(stmt)
    conn.commit()


def _build_db(tmp_path: Path, *seed_files: Path) -> Path:
    """Create a fresh SQLite database and apply schema + seed files."""
    db_path = tmp_path / "test.sqlite"
    conn = sqlite3.connect(str(db_path))
    _apply_sql_file(conn, FIXTURES_DIR / "schema.sql")
    for seed in seed_files:
        _apply_sql_file(conn, seed)
    conn.close()
    return db_path


def _run_cli(db_path: Path, expect_path: Path, fmt: str = "text"):
    """Run the CLI tool and return CompletedProcess."""
    return subprocess.run(
        [
            sys.executable,
            "-m",
            "tools.sqlite_state_assert",
            "--db",
            str(db_path),
            "--expect",
            str(expect_path),
            "--format",
            fmt,
        ],
        capture_output=True,
        text=True,
        cwd=str(Path(__file__).parent.parent.parent),  # repo root
    )


class TestExactPass:
    def test_exact_pass_returns_exit_code_0(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_exact_pass.json")
        assert result.returncode == 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"

    def test_exact_pass_output_contains_pass(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_exact_pass.json")
        assert "PASS" in result.stdout


class TestExactFail:
    def test_exact_fail_returns_exit_code_1(self, tmp_path):
        # seed_invalid has wrong target_url -> exact match should fail
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_invalid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_exact_fail.json")
        assert result.returncode == 1, f"stdout: {result.stdout}\nstderr: {result.stderr}"

    def test_exact_fail_output_contains_fail(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_invalid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_exact_fail.json")
        assert "FAIL" in result.stdout


class TestContainsPass:
    def test_contains_pass_returns_exit_code_0(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_contains_pass.json")
        assert result.returncode == 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"


class TestMatchersPass:
    def test_matchers_pass_returns_exit_code_0(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_matchers_pass.json")
        assert result.returncode == 0, f"stdout: {result.stdout}\nstderr: {result.stderr}"


class TestInvalidExpectation:
    def test_invalid_schema_returns_exit_code_2(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_invalid_schema.json")
        assert result.returncode == 2, f"stdout: {result.stdout}\nstderr: {result.stderr}"

    def test_missing_db_returns_exit_code_2(self, tmp_path):
        missing_db = tmp_path / "nonexistent.sqlite"
        result = _run_cli(missing_db, FIXTURES_DIR / "expected_exact_pass.json")
        assert result.returncode == 2

    def test_missing_expect_returns_exit_code_2(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        missing_expect = tmp_path / "nonexistent.json"
        result = _run_cli(db_path, missing_expect)
        assert result.returncode == 2


class TestJsonOutput:
    def test_json_output_is_valid_json(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_exact_pass.json", fmt="json")
        assert result.returncode == 0
        parsed = json.loads(result.stdout)
        assert "status" in parsed
        assert "tables" in parsed
        assert parsed["status"] == "pass"

    def test_json_output_fail_is_valid_json(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_invalid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_exact_fail.json", fmt="json")
        assert result.returncode == 1
        parsed = json.loads(result.stdout)
        assert parsed["status"] == "fail"
        assert len(parsed["tables"]) > 0

    def test_json_output_structure(self, tmp_path):
        db_path = _build_db(tmp_path, FIXTURES_DIR / "seed_valid.sql")
        result = _run_cli(db_path, FIXTURES_DIR / "expected_exact_pass.json", fmt="json")
        parsed = json.loads(result.stdout)
        assert "database" in parsed
        assert "expectation" in parsed
        for table in parsed["tables"]:
            assert "name" in table
            assert "status" in table
            assert "mode" in table
            assert "expected_count" in table
            assert "actual_count" in table
