"""SQLite Database State Assertion Tool.

CLI tool that verifies the factual state of a SQLite database against a JSON
expectation file. Returns exit code 0 on pass, 1 on mismatch, 2 on error.

Usage:
    python -m tools.sqlite_state_assert --db path/to/test.sqlite --expect expected.json
"""
from __future__ import annotations

import argparse
import difflib
import json
import re
import sqlite3
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SUPPORTED_VERSIONS = {1}
SUPPORTED_MODES = {"exact", "contains"}
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")

EXIT_PASS = 0
EXIT_FAIL = 1
EXIT_ERROR = 2

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
# CLI entry point
# ---------------------------------------------------------------------------


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Assert SQLite database state against a JSON expectation file.",
        prog="python -m tools.sqlite_state_assert",
    )
    parser.add_argument("--db", required=True, help="Path to the SQLite database file.")
    parser.add_argument("--expect", required=True, help="Path to the JSON expectation file.")
    parser.add_argument(
        "--format",
        choices=["text", "json"],
        default="text",
        help="Output format (default: text).",
    )

    args = parser.parse_args(argv)
    db_path = Path(args.db)
    expect_path = Path(args.expect)

    # Validate inputs
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

    if args.format == "json":
        print(render_json_result(result))
    else:
        print(render_text_result(result))

    return EXIT_PASS if result.status == "pass" else EXIT_FAIL


if __name__ == "__main__":
    sys.exit(main())
