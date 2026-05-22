"""SQLite Database State Assertion Tool.

CLI tool that verifies the factual state of a SQLite database against a JSON
expectation file. Returns exit code 0 on pass, 1 on mismatch, 2 on error.

Usage:
    python -m tools.sqlite_state_assert --db path/to/test.sqlite --expect expected.json
"""
from __future__ import annotations

import argparse
import datetime as dt
import difflib
import json
import os
import re
import shutil
import sqlite3
import socket
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any
from urllib import error as urlerror
from urllib import request as urlrequest

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SUPPORTED_VERSIONS = {1}
SUPPORTED_MODES = {"exact", "contains"}
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_ERROR = 2

DEFAULT_PRESERVE_TABLES = {
    "app_users",
    "auth_audit_log",
    "service_clients",
    "user_credentials",
    "users",
    "credentials",
}

# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class TableExpectation:
    name: str
    mode: str
    columns: list[str] | None
    ignore_columns: list[str]
    order_by: list[str]
    rows: list[dict[str, Any]]


@dataclass
class TableComparisonResult:
    table: str
    status: str
    mode: str
    expected_count: int
    actual_count: int
    message: str
    expected_rows: list[dict[str, Any]]
    actual_rows: list[dict[str, Any]]
    diff: str | None


@dataclass
class DatabaseComparisonResult:
    status: str
    database: str
    expectation: str
    tables: list[TableComparisonResult] = field(default_factory=list)


# ---------------------------------------------------------------------------
# Identifier safety
# ---------------------------------------------------------------------------


def _validate_identifier(name: str, context: str) -> None:
    """Raise ValueError if *name* is not a safe SQL identifier."""
    if not IDENTIFIER_RE.match(name):
        raise ValueError(
            f"Invalid SQL identifier {name!r} in {context}. "
            "Identifiers must match ^[A-Za-z_][A-Za-z0-9_]*$"
        )


def _quote(identifier: str) -> str:
    """Return a double-quoted SQL identifier (already validated)."""
    return f'"{identifier}"'


# ---------------------------------------------------------------------------
# JSON expectation loading / validation / parsing
# ---------------------------------------------------------------------------


def load_expectation(path: Path) -> dict[str, Any]:
    """Load and JSON-parse an expectation file. Raises on I/O or parse error."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        raise OSError(f"Cannot read expectation file {path}: {exc}") from exc
    try:
        return json.loads(text)
    except json.JSONDecodeError as exc:
        raise ValueError(f"Invalid JSON in expectation file {path}: {exc}") from exc


def validate_expectation(raw: dict[str, Any]) -> None:
    """Validate the structure of a raw expectation dict.

    Raises ValueError with a descriptive message on the first problem found.
    """
    if not isinstance(raw, dict):
        raise ValueError("Expectation must be a JSON object")

    # version
    if "version" not in raw:
        raise ValueError("Expectation is missing required field 'version'")
    if raw["version"] not in SUPPORTED_VERSIONS:
        raise ValueError(
            f"Unsupported expectation version {raw['version']!r}. "
            f"Supported: {sorted(SUPPORTED_VERSIONS)}"
        )

    # tables
    if "tables" not in raw:
        raise ValueError("Expectation is missing required field 'tables'")
    if not isinstance(raw["tables"], dict):
        raise ValueError("'tables' must be a JSON object")

    for table_name, table_spec in raw["tables"].items():
        ctx = f"tables.{table_name}"
        try:
            _validate_identifier(table_name, ctx)
        except ValueError as exc:
            raise ValueError(str(exc)) from exc

        if not isinstance(table_spec, dict):
            raise ValueError(f"{ctx}: table spec must be a JSON object")

        # mode
        mode = table_spec.get("mode", "exact")
        if mode not in SUPPORTED_MODES:
            raise ValueError(
                f"{ctx}.mode: unsupported mode {mode!r}. "
                f"Supported: {sorted(SUPPORTED_MODES)}"
            )

        # columns
        columns = table_spec.get("columns")
        if columns is not None:
            if not isinstance(columns, list):
                raise ValueError(f"{ctx}.columns: must be a list")
            for col in columns:
                _validate_identifier(col, f"{ctx}.columns")

        # ignore_columns
        ignore_columns = table_spec.get("ignore_columns", [])
        if not isinstance(ignore_columns, list):
            raise ValueError(f"{ctx}.ignore_columns: must be a list")
        for col in ignore_columns:
            _validate_identifier(col, f"{ctx}.ignore_columns")

        # order_by
        order_by = table_spec.get("order_by", [])
        if not isinstance(order_by, list):
            raise ValueError(f"{ctx}.order_by: must be a list")
        for col in order_by:
            _validate_identifier(col, f"{ctx}.order_by")

        # rows
        rows = table_spec.get("rows", [])
        if not isinstance(rows, list):
            raise ValueError(f"{ctx}.rows: must be a list")
        for idx, row in enumerate(rows):
            if not isinstance(row, dict):
                raise ValueError(f"{ctx}.rows[{idx}]: each row must be a JSON object")


def parse_expectation(raw: dict[str, Any]) -> list[TableExpectation]:
    """Convert validated raw expectation dict to list of TableExpectation."""
    result: list[TableExpectation] = []
    for table_name, spec in raw["tables"].items():
        result.append(
            TableExpectation(
                name=table_name,
                mode=spec.get("mode", "exact"),
                columns=spec.get("columns"),
                ignore_columns=spec.get("ignore_columns", []),
                order_by=spec.get("order_by", []),
                rows=spec.get("rows", []),
            )
        )
    return result


# ---------------------------------------------------------------------------
# SQLite reading
# ---------------------------------------------------------------------------


def _normalize_value(value: Any) -> Any:
    """Normalize a value returned by sqlite3 to a JSON-compatible type."""
    if value is None:
        return None
    if isinstance(value, (int, float, str)):
        return value
    if isinstance(value, bytes):
        return value.hex()
    # fallback – convert to string
    return str(value)


def _get_all_columns(conn: sqlite3.Connection, table_name: str) -> list[str]:
    """Return all column names for *table_name* using PRAGMA table_info."""
    cursor = conn.execute(f"PRAGMA table_info({_quote(table_name)})")
    return [row[1] for row in cursor.fetchall()]


def read_table_rows(
    conn: sqlite3.Connection,
    table: TableExpectation,
) -> list[dict[str, Any]]:
    """Query the database and return normalised rows for *table*.

    Columns are selected according to table.columns / table.ignore_columns.
    Rows are ordered according to table.order_by.
    """
    # Determine which columns to select
    if table.columns is not None:
        select_cols = [c for c in table.columns if c not in table.ignore_columns]
    else:
        all_cols = _get_all_columns(conn, table.name)
        select_cols = [c for c in all_cols if c not in table.ignore_columns]

    if not select_cols:
        raise ValueError(
            f"Table '{table.name}': no columns to select after applying ignore_columns"
        )

    col_sql = ", ".join(_quote(c) for c in select_cols)
    sql = f"SELECT {col_sql} FROM {_quote(table.name)}"
    if table.order_by:
        order_sql = ", ".join(_quote(c) for c in table.order_by)
        sql += f" ORDER BY {order_sql}"

    try:
        cursor = conn.execute(sql)
    except sqlite3.OperationalError as exc:
        raise sqlite3.OperationalError(
            f"Error querying table '{table.name}': {exc}"
        ) from exc

    rows = []
    for raw_row in cursor.fetchall():
        rows.append(
            {col: _normalize_value(val) for col, val in zip(select_cols, raw_row)}
        )
    return rows


# ---------------------------------------------------------------------------
# Value matchers
# ---------------------------------------------------------------------------

_SUPPORTED_MATCHERS = {"$any", "$not_null", "$regex", "$contains", "$gt", "$gte", "$lt", "$lte"}


def _match_value(expected: Any, actual: Any) -> tuple[bool, str]:
    """Return (matched, description) for an expected/actual pair.

    *expected* may be a literal value or a matcher dict such as {"$gte": 1}.
    """
    if isinstance(expected, dict) and len(expected) == 1:
        key = next(iter(expected))
        if key in _SUPPORTED_MATCHERS:
            return _apply_matcher(key, expected[key], actual)
        if key.startswith("$"):
            return False, f"Unsupported matcher {key!r}"

    # Literal equality
    if expected == actual:
        return True, ""
    return False, f"expected {expected!r}, got {actual!r}"


def _apply_matcher(op: str, operand: Any, actual: Any) -> tuple[bool, str]:
    if op == "$any":
        return True, ""

    if op == "$not_null":
        if actual is not None:
            return True, ""
        return False, "expected non-null value, got null"

    if op == "$regex":
        if not isinstance(actual, str):
            return False, f"$regex requires a string value, got {type(actual).__name__}"
        if re.search(operand, actual):
            return True, ""
        return False, f"value {actual!r} does not match regex '{operand}'"

    if op == "$contains":
        if not isinstance(actual, str):
            return False, f"$contains requires a string value, got {type(actual).__name__}"
        if operand in actual:
            return True, ""
        return False, f"value {actual!r} does not contain {operand!r}"

    if op in ("$gt", "$gte", "$lt", "$lte"):
        try:
            cmp = float(actual) if isinstance(actual, (int, float)) else None
        except (TypeError, ValueError):
            cmp = None
        if cmp is None:
            return False, f"{op} requires a numeric value, got {actual!r}"
        ops_map = {
            "$gt": (lambda a, b: a > b, ">"),
            "$gte": (lambda a, b: a >= b, ">="),
            "$lt": (lambda a, b: a < b, "<"),
            "$lte": (lambda a, b: a <= b, "<="),
        }
        fn, symbol = ops_map[op]
        if fn(actual, operand):
            return True, ""
        return False, f"expected value {symbol} {operand}, got {actual!r}"

    return False, f"Unsupported matcher {op!r}"


def _rows_match(expected_row: dict[str, Any], actual_row: dict[str, Any]) -> tuple[bool, list[str]]:
    """Return (all_match, list_of_failure_descriptions)."""
    failures = []
    for col, exp_val in expected_row.items():
        act_val = actual_row.get(col)
        ok, msg = _match_value(exp_val, act_val)
        if not ok:
            failures.append(f"  column {col!r}: {msg}")
    return len(failures) == 0, failures


# ---------------------------------------------------------------------------
# Comparison
# ---------------------------------------------------------------------------


def _sort_rows(rows: list[dict[str, Any]]) -> list[dict[str, Any]]:
    """Sort rows deterministically by their JSON representation."""
    return sorted(rows, key=lambda r: json.dumps(r, sort_keys=True))


def _make_diff(expected_rows: list[dict[str, Any]], actual_rows: list[dict[str, Any]]) -> str:
    """Generate a unified diff between expected and actual row lists."""
    exp_text = json.dumps(expected_rows, indent=2, sort_keys=True).splitlines(keepends=True)
    act_text = json.dumps(actual_rows, indent=2, sort_keys=True).splitlines(keepends=True)
    lines = list(
        difflib.unified_diff(exp_text, act_text, fromfile="expected", tofile="actual")
    )
    return "".join(lines)


def compare_table(
    expected: TableExpectation,
    actual_rows: list[dict[str, Any]],
) -> TableComparisonResult:
    """Compare expected table spec against actual rows and return a result."""
    # Filter ignore_columns from actual_rows
    ignore = set(expected.ignore_columns)
    actual_filtered = [
        {k: v for k, v in row.items() if k not in ignore} for row in actual_rows
    ]

    # If no order_by, sort deterministically
    if not expected.order_by:
        actual_filtered = _sort_rows(actual_filtered)
        expected_rows = _sort_rows(expected.rows)
    else:
        expected_rows = expected.rows

    if expected.mode == "exact":
        return _compare_exact(expected, expected_rows, actual_filtered)
    else:
        return _compare_contains(expected, expected_rows, actual_filtered)


def _compare_exact(
    expected: TableExpectation,
    expected_rows: list[dict[str, Any]],
    actual_rows: list[dict[str, Any]],
) -> TableComparisonResult:
    exp_count = len(expected_rows)
    act_count = len(actual_rows)

    # Check row count first
    if exp_count != act_count:
        diff = _make_diff(expected_rows, actual_rows)
        return TableComparisonResult(
            table=expected.name,
            status="fail",
            mode="exact",
            expected_count=exp_count,
            actual_count=act_count,
            message=f"Row count mismatch: expected {exp_count}, got {act_count}",
            expected_rows=expected_rows,
            actual_rows=actual_rows,
            diff=diff,
        )

    # Compare row by row
    all_match = True
    for exp_row, act_row in zip(expected_rows, actual_rows):
        ok, _ = _rows_match(exp_row, act_row)
        if not ok:
            all_match = False
            break

    if all_match:
        return TableComparisonResult(
            table=expected.name,
            status="pass",
            mode="exact",
            expected_count=exp_count,
            actual_count=act_count,
            message="",
            expected_rows=expected_rows,
            actual_rows=actual_rows,
            diff=None,
        )

    diff = _make_diff(expected_rows, actual_rows)
    return TableComparisonResult(
        table=expected.name,
        status="fail",
        mode="exact",
        expected_count=exp_count,
        actual_count=act_count,
        message="Rows differ",
        expected_rows=expected_rows,
        actual_rows=actual_rows,
        diff=diff,
    )


def _compare_contains(
    expected: TableExpectation,
    expected_rows: list[dict[str, Any]],
    actual_rows: list[dict[str, Any]],
) -> TableComparisonResult:
    exp_count = len(expected_rows)
    act_count = len(actual_rows)

    missing: list[dict[str, Any]] = []
    for exp_row in expected_rows:
        found = False
        for act_row in actual_rows:
            # Only compare columns present in expected row
            act_subset = {k: act_row.get(k) for k in exp_row}
            ok, _ = _rows_match(exp_row, act_subset)
            if ok:
                found = True
                break
        if not found:
            missing.append(exp_row)

    if not missing:
        return TableComparisonResult(
            table=expected.name,
            status="pass",
            mode="contains",
            expected_count=exp_count,
            actual_count=act_count,
            message="",
            expected_rows=expected_rows,
            actual_rows=actual_rows,
            diff=None,
        )

    diff = _make_diff(missing, [])
    return TableComparisonResult(
        table=expected.name,
        status="fail",
        mode="contains",
        expected_count=exp_count,
        actual_count=act_count,
        message=f"{len(missing)} expected row(s) not found in actual data",
        expected_rows=expected_rows,
        actual_rows=actual_rows,
        diff=diff,
    )


# ---------------------------------------------------------------------------
# Top-level orchestration
# ---------------------------------------------------------------------------


def compare_database(
    db_path: Path,
    expectation_path: Path,
) -> DatabaseComparisonResult:
    """Load expectation, query database, compare, and return aggregated result."""
    raw = load_expectation(expectation_path)
    validate_expectation(raw)
    table_expectations = parse_expectation(raw)

    conn = sqlite3.connect(str(db_path))
    try:
        table_results: list[TableComparisonResult] = []
        for texp in table_expectations:
            actual_rows = read_table_rows(conn, texp)
            result = compare_table(texp, actual_rows)
            table_results.append(result)
    finally:
        conn.close()

    overall = "pass" if all(r.status == "pass" for r in table_results) else "fail"
    return DatabaseComparisonResult(
        status=overall,
        database=str(db_path),
        expectation=str(expectation_path),
        tables=table_results,
    )


# ---------------------------------------------------------------------------
# Output renderers
# ---------------------------------------------------------------------------


def render_text_result(result: DatabaseComparisonResult) -> str:
    """Render a human-readable text report."""
    lines: list[str] = []
    status_label = "PASS" if result.status == "pass" else "FAIL"
    lines.append(f"SQLite state assertion: {status_label}")
    lines.append("")
    lines.append(f"Database: {result.database}")
    lines.append(f"Expectation: {result.expectation}")
    lines.append("")
    lines.append("Tables:")

    for t in result.tables:
        t_label = "PASS" if t.status == "pass" else "FAIL"
        lines.append(
            f"  {t.table:<20} {t_label} {t.mode:<8} "
            f"expected={t.expected_count} actual={t.actual_count}"
        )

    for t in result.tables:
        if t.status == "fail":
            lines.append("")
            lines.append(f"Diff for table: {t.table}")
            if t.message:
                lines.append(f"  {t.message}")
            lines.append("")
            lines.append("Expected:")
            lines.append(json.dumps(t.expected_rows, indent=2, sort_keys=True))
            lines.append("")
            lines.append("Actual:")
            lines.append(json.dumps(t.actual_rows, indent=2, sort_keys=True))
            if t.diff:
                lines.append("")
                lines.append("Unified diff:")
                lines.append("--- expected")
                lines.append("+++ actual")
                lines.append(t.diff)

    return "\n".join(lines)


def render_json_result(result: DatabaseComparisonResult) -> str:
    """Render a machine-readable JSON report."""
    tables = []
    for t in result.tables:
        tables.append(
            {
                "name": t.table,
                "status": t.status,
                "mode": t.mode,
                "expected_count": t.expected_count,
                "actual_count": t.actual_count,
                "message": t.message,
            }
        )
    obj = {
        "status": result.status,
        "database": result.database,
        "expectation": result.expectation,
        "tables": tables,
    }
    return json.dumps(obj, indent=2)


# ---------------------------------------------------------------------------
# Runtime / network / process / log assertions
# ---------------------------------------------------------------------------


def _now_utc() -> str:
    return dt.datetime.now(tz=dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")


def _ensure_failure_tree(base_dir: Path) -> tuple[Path, Path, Path, Path, Path]:
    run_dir = base_dir / _now_utc()
    logs_dir = run_dir / "logs"
    db_dir = run_dir / "database"
    stdout_dir = run_dir / "stdout"
    stderr_dir = run_dir / "stderr"
    for path in (logs_dir, db_dir, stdout_dir, stderr_dir):
        path.mkdir(parents=True, exist_ok=True)
    return run_dir, logs_dir, db_dir, stdout_dir, stderr_dir


def _list_tables(conn: sqlite3.Connection) -> list[str]:
    cur = conn.execute(
        "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY name"
    )
    return [r[0] for r in cur.fetchall()]


def _snapshot_database(db_path: Path, output_path: Path) -> None:
    if not db_path.exists():
        return
    conn = sqlite3.connect(str(db_path))
    conn.row_factory = sqlite3.Row
    snapshot: dict[str, Any] = {"database": str(db_path), "tables": {}}
    try:
        for table in _list_tables(conn):
            cols = _get_all_columns(conn, table)
            col_sql = ", ".join(_quote(c) for c in cols)
            rows = conn.execute(f"SELECT {col_sql} FROM {_quote(table)}").fetchall()
            snapshot["tables"][table] = [
                {c: _normalize_value(r[c]) for c in cols} for r in rows
            ]
    finally:
        conn.close()
    output_path.write_text(json.dumps(snapshot, indent=2, sort_keys=True), encoding="utf-8")


def _record_failure_artifacts(
    artifacts_root: Path,
    summary: dict[str, Any],
    db_path: Path | None = None,
    log_path: Path | None = None,
    stdout_text: str = "",
    stderr_text: str = "",
) -> Path:
    run_dir, logs_dir, db_dir, stdout_dir, stderr_dir = _ensure_failure_tree(artifacts_root)

    if db_path is not None:
        _snapshot_database(db_path, db_dir / "db_snapshot.json")

    if log_path is not None and log_path.exists():
        shutil.copy2(log_path, logs_dir / log_path.name)

    (stdout_dir / "cli_output.txt").write_text(stdout_text, encoding="utf-8")
    (stderr_dir / "cli_error.txt").write_text(stderr_text, encoding="utf-8")
    (run_dir / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    return run_dir


def _retry_assertion(
    check_fn,
    timeout_seconds: float,
    initial_delay_seconds: float,
) -> tuple[bool, str]:
    deadline = time.monotonic() + timeout_seconds
    delay = max(0.05, initial_delay_seconds)
    last_message = ""
    while True:
        ok, msg = check_fn()
        if ok:
            return True, msg
        last_message = msg
        if time.monotonic() >= deadline:
            return False, last_message
        time.sleep(delay)
        delay = min(delay * 2.0, 2.0)


def assert_port_open(port: int, host: str = "127.0.0.1") -> tuple[bool, str]:
    try:
        with socket.create_connection((host, port), timeout=1.0):
            return True, f"Port {port} is open"
    except OSError as exc:
        return False, f"Port {port} is not open: {exc}"


def assert_port_closed(port: int, host: str = "127.0.0.1") -> tuple[bool, str]:
    ok, msg = assert_port_open(port, host=host)
    if ok:
        return False, f"Port {port} is unexpectedly open"
    return True, f"Port {port} is closed"


def _http_open(url: str, follow_redirects: bool = True) -> tuple[int, dict[str, str], str]:
    class _NoRedirect(urlrequest.HTTPRedirectHandler):
        def redirect_request(self, req, fp, code, msg, headers, newurl):
            return None

    opener = (
        urlrequest.build_opener()
        if follow_redirects
        else urlrequest.build_opener(_NoRedirect())
    )
    req = urlrequest.Request(url, method="GET")
    try:
        with opener.open(req, timeout=5.0) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            return resp.status, dict(resp.headers.items()), body
    except urlerror.HTTPError as exc:
        body = exc.read().decode("utf-8", errors="replace")
        return exc.code, dict(exc.headers.items()), body


def assert_http_status(url: str, expected_status: int) -> tuple[bool, str]:
    status, _, _ = _http_open(url, follow_redirects=False)
    if status == expected_status:
        return True, f"HTTP status {status} matched expected"
    return False, f"Expected HTTP status {expected_status}, got {status}"


def assert_redirect_target(url: str, expected_target: str) -> tuple[bool, str]:
    status, headers, _ = _http_open(url, follow_redirects=False)
    actual = headers.get("Location") or headers.get("location")
    if status not in (301, 302, 303, 307, 308):
        return False, f"Expected redirect response, got HTTP {status}"
    if actual == expected_target:
        return True, f"Redirect target matched: {actual}"
    return False, f"Expected redirect target {expected_target!r}, got {actual!r}"


def assert_http_body_contains(url: str, expected_substring: str) -> tuple[bool, str]:
    _, _, body = _http_open(url, follow_redirects=True)
    if expected_substring in body:
        return True, "HTTP response body contains expected text"
    return False, f"Response body does not contain {expected_substring!r}"


def assert_process_running(name: str) -> tuple[bool, str]:
    proc_root = Path("/proc")
    for entry in proc_root.iterdir():
        if not entry.name.isdigit():
            continue
        try:
            comm = (entry / "comm").read_text(encoding="utf-8").strip()
            cmdline = (entry / "cmdline").read_text(encoding="utf-8").replace("\x00", " ")
        except OSError:
            continue
        if name in comm or name in cmdline:
            return True, f"Process matching {name!r} is running (pid={entry.name})"
    return False, f"No running process matched {name!r}"


def assert_log_contains(log_file: Path, expected_text: str) -> tuple[bool, str]:
    if not log_file.exists():
        return False, f"Log file not found: {log_file}"
    text = log_file.read_text(encoding="utf-8", errors="replace")
    if expected_text in text:
        return True, "Expected text found in log"
    return False, f"Text {expected_text!r} not found in log {log_file}"


def reset_database(db_path: Path, preserve_tables: set[str] | None = None) -> tuple[list[str], list[str]]:
    if preserve_tables is None:
        preserve_tables = set(DEFAULT_PRESERVE_TABLES)
    conn = sqlite3.connect(str(db_path))
    try:
        all_tables = _list_tables(conn)
        preserved = [t for t in all_tables if t in preserve_tables]
        cleared = [t for t in all_tables if t not in preserve_tables]
        for table in cleared:
            conn.execute(f"DELETE FROM {_quote(table)};")
        if "sqlite_sequence" in all_tables and cleared:
            for table in cleared:
                conn.execute("DELETE FROM sqlite_sequence WHERE name = ?;", (table,))
        conn.commit()
        return sorted(cleared), sorted(preserved)
    finally:
        conn.close()


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Assert SQLite database state and runtime/network/process/log conditions "
            "for deterministic QA scenarios."
        ),
        prog="python -m tools.sqlite_state_assert",
    )
    parser.add_argument("--db", help="Path to the SQLite database file.")
    parser.add_argument("--expect", help="Path to the JSON expectation file.")
    parser.add_argument(
        "--format",
        choices=["text", "json"],
        default="text",
        help="Output format (default: text).",
    )
    parser.add_argument("--reset-database", action="store_true", help="Reset transient DB tables.")
    parser.add_argument("--assert-port-open", type=int, metavar="PORT")
    parser.add_argument("--assert-port-closed", type=int, metavar="PORT")
    parser.add_argument(
        "--assert-http-status",
        nargs=2,
        metavar=("URL", "STATUS"),
        help="Assert URL responds with expected status.",
    )
    parser.add_argument(
        "--assert-redirect-target",
        nargs=2,
        metavar=("URL", "TARGET"),
        help="Assert URL redirects to expected target.",
    )
    parser.add_argument(
        "--assert-http-body-contains",
        nargs=2,
        metavar=("URL", "TEXT"),
        help="Assert HTTP response body contains TEXT.",
    )
    parser.add_argument(
        "--assert-process-running",
        metavar="PROCESS_NAME",
        help="Assert a process is running by command name fragment.",
    )
    parser.add_argument(
        "--assert-log-contains",
        nargs=2,
        metavar=("LOG_FILE", "TEXT"),
        help="Assert log file contains TEXT.",
    )
    parser.add_argument(
        "--retry-timeout-seconds",
        type=float,
        default=30.0,
        help="Maximum retry duration for runtime assertions (default: 30).",
    )
    parser.add_argument(
        "--retry-initial-delay-seconds",
        type=float,
        default=0.25,
        help="Initial retry delay before exponential backoff (default: 0.25).",
    )
    parser.add_argument(
        "--failure-artifacts-dir",
        default="qa_failures",
        help="Failure artifact root directory (default: qa_failures).",
    )

    args = parser.parse_args(argv)
    artifacts_root = Path(args.failure_artifacts_dir)
    db_path = Path(args.db) if args.db else None

    runtime_checks = [
        args.assert_port_open is not None,
        args.assert_port_closed is not None,
        args.assert_http_status is not None,
        args.assert_redirect_target is not None,
        args.assert_http_body_contains is not None,
        args.assert_process_running is not None,
        args.assert_log_contains is not None,
    ]
    selected_operations = sum(
        [
            args.reset_database,
            bool(args.expect),
            any(runtime_checks),
        ]
    )
    if selected_operations == 0:
        print(
            "ERROR: No operation selected. Use --expect, --reset-database, or one assertion flag.",
            file=sys.stderr,
        )
        return EXIT_ERROR
    if selected_operations > 1:
        print(
            "ERROR: Choose exactly one operation per invocation.",
            file=sys.stderr,
        )
        return EXIT_ERROR

    if any(runtime_checks) and sum(1 for x in runtime_checks if x) > 1:
        print(
            "ERROR: Provide exactly one runtime assertion option per invocation.",
            file=sys.stderr,
        )
        return EXIT_ERROR

    if args.reset_database:
        if db_path is None:
            print("ERROR: --db is required with --reset-database", file=sys.stderr)
            return EXIT_ERROR
        if not db_path.exists():
            print(f"ERROR: Database file not found: {db_path}", file=sys.stderr)
            return EXIT_ERROR
        try:
            cleared, preserved = reset_database(db_path)
        except sqlite3.OperationalError as exc:
            print(f"ERROR: SQLite error during reset: {exc}", file=sys.stderr)
            return EXIT_ERROR
        payload = {
            "status": "pass",
            "operation": "reset_database",
            "database": str(db_path),
            "cleared_tables": cleared,
            "preserved_tables": preserved,
        }
        if args.format == "json":
            print(json.dumps(payload, indent=2))
        else:
            print("SQLite state assertion: PASS")
            print("")
            print(f"Database reset completed: {db_path}")
            print(f"Cleared tables: {', '.join(cleared) if cleared else '(none)'}")
            print(f"Preserved tables: {', '.join(preserved) if preserved else '(none)'}")
        return EXIT_PASS

    if args.expect:
        if db_path is None:
            print("ERROR: --db is required with --expect", file=sys.stderr)
            return EXIT_ERROR
        expect_path = Path(args.expect)
        if not db_path.exists():
            print(f"ERROR: Database file not found: {db_path}", file=sys.stderr)
            return EXIT_ERROR
        if not expect_path.exists():
            print(f"ERROR: Expectation file not found: {expect_path}", file=sys.stderr)
            return EXIT_ERROR
        try:
            result = compare_database(db_path, expect_path)
        except (ValueError, OSError) as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return EXIT_ERROR
        except sqlite3.OperationalError as exc:
            print(f"ERROR: SQLite error: {exc}", file=sys.stderr)
            return EXIT_ERROR
        output = render_json_result(result) if args.format == "json" else render_text_result(result)
        print(output)
        if result.status == "pass":
            return EXIT_PASS
        failure_dir = _record_failure_artifacts(
            artifacts_root=artifacts_root,
            summary={
                "status": "fail",
                "operation": "db_expectation",
                "database": str(db_path),
                "expectation": str(expect_path),
            },
            db_path=db_path,
            stdout_text=output,
            stderr_text="",
        )
        print(f"Failure artifacts saved to: {failure_dir}", file=sys.stderr)
        return EXIT_FAIL

    # Runtime assertion mode.
    check_name = ""
    log_path: Path | None = None
    if args.assert_port_open is not None:
        check_name = "assert_port_open"
        def check_fn() -> tuple[bool, str]:
            return assert_port_open(args.assert_port_open)
    elif args.assert_port_closed is not None:
        check_name = "assert_port_closed"
        def check_fn() -> tuple[bool, str]:
            return assert_port_closed(args.assert_port_closed)
    elif args.assert_http_status is not None:
        check_name = "assert_http_status"
        url, status = args.assert_http_status
        expected_status = int(status)
        def check_fn() -> tuple[bool, str]:
            return assert_http_status(url, expected_status)
    elif args.assert_redirect_target is not None:
        check_name = "assert_redirect_target"
        url, target = args.assert_redirect_target
        def check_fn() -> tuple[bool, str]:
            return assert_redirect_target(url, target)
    elif args.assert_http_body_contains is not None:
        check_name = "assert_http_body_contains"
        url, text = args.assert_http_body_contains
        def check_fn() -> tuple[bool, str]:
            return assert_http_body_contains(url, text)
    elif args.assert_process_running is not None:
        check_name = "assert_process_running"
        def check_fn() -> tuple[bool, str]:
            return assert_process_running(args.assert_process_running)
    elif args.assert_log_contains is not None:
        check_name = "assert_log_contains"
        log_file, text = args.assert_log_contains
        log_path = Path(log_file)
        def check_fn() -> tuple[bool, str]:
            return assert_log_contains(Path(log_file), text)
    else:
        print("ERROR: Internal CLI state error.", file=sys.stderr)
        return EXIT_ERROR

    try:
        passed, message = _retry_assertion(
            check_fn=check_fn,
            timeout_seconds=args.retry_timeout_seconds,
            initial_delay_seconds=args.retry_initial_delay_seconds,
        )
    except Exception as exc:  # pragma: no cover - defensive safety net
        print(f"ERROR: Runtime assertion failed with unexpected error: {exc}", file=sys.stderr)
        return EXIT_ERROR

    result_obj = {"status": "pass" if passed else "fail", "operation": check_name, "message": message}
    output = json.dumps(result_obj, indent=2) if args.format == "json" else message
    if passed:
        print(output)
        return EXIT_PASS

    failure_dir = _record_failure_artifacts(
        artifacts_root=artifacts_root,
        summary=result_obj,
        db_path=db_path if db_path and db_path.exists() else None,
        log_path=log_path,
        stdout_text=output,
        stderr_text=message,
    )
    print(output, file=sys.stderr)
    print(f"Failure artifacts saved to: {failure_dir}", file=sys.stderr)
    return EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
