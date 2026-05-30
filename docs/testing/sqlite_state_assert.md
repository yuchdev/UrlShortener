# SQLite State Assertion Tool

## Purpose

`tools/sqlite_state_assert` is a Python CLI tool that verifies the factual state of a SQLite database against a JSON expectation file. It is designed for integration tests where a test scenario performs actions on a clean database and then this tool validates the resulting state.

The tool is generic and can validate any SQLite database table, not just those used by the URL shortener application.

---

## When to Use It

Use this tool **after** your integration test scenario has performed its actions. The tool:

- Opens an existing SQLite database
- Loads an expectation JSON file
- Reads the specified tables and columns
- Compares actual rows against expected rows
- Prints pass/fail output with diffs
- Returns CI-friendly exit codes

---

## How to Invoke It

```bash
python -m tools.sqlite_state_assert \
  --db path/to/test.sqlite \
  --expect tests/integration/expected/create_short_url.json
```

### Required Arguments (DB snapshot mode)

| Argument   | Description                          |
|------------|--------------------------------------|
| `--db`     | Path to the SQLite database file.    |
| `--expect` | Path to the JSON expectation file.   |

### Optional Arguments

| Argument         | Description                           | Default |
|------------------|---------------------------------------|---------|
| `--format text`  | Human-readable output (default).      | text    |
| `--format json`  | Machine-readable JSON output.         |         |
| `--reset-database` | Reset transient tables while preserving auth identity tables. | |
| `--assert-port-open PORT` | Assert local TCP port is open. | |
| `--assert-port-closed PORT` | Assert local TCP port is closed. | |
| `--assert-http-status URL STATUS` | Assert URL returns specific status code. | |
| `--assert-redirect-target URL TARGET` | Assert redirect Location target. | |
| `--assert-http-body-contains URL TEXT` | Assert response body contains text. | |
| `--assert-process-running NAME` | Assert process is running by name fragment. | |
| `--assert-log-contains FILE TEXT` | Assert log file contains text. | |
| `--retry-timeout-seconds` | Runtime assertion retry timeout. | 30 |
| `--retry-initial-delay-seconds` | Runtime assertion initial delay for exponential backoff. | 0.25 |
| `--failure-artifacts-dir` | Root directory for failure diagnostics. | qa_failures |

---

## Exit Codes

| Code | Meaning                                              |
|------|------------------------------------------------------|
| `0`  | Assertion/reset operation passed.                     |
| `1`  | Assertion failed.                                     |
| `2`  | Error: invalid arguments, missing files, SQL/runtime errors. |

---

## Runtime Assertions and Reset Examples

```bash
# Reset transient DB tables
python -m tools.sqlite_state_assert --db qa/tmp/qa.sqlite --reset-database

# Network/runtime checks
python -m tools.sqlite_state_assert --assert-port-open 8080
python -m tools.sqlite_state_assert --assert-http-status http://127.0.0.1:8080/health 200
python -m tools.sqlite_state_assert --assert-redirect-target http://127.0.0.1:8080/r/demo https://example.com
python -m tools.sqlite_state_assert --assert-process-running url_shortener
python -m tools.sqlite_state_assert --assert-log-contains qa/tmp/server.log "Server started"
```

On failure, diagnostics are stored under `qa_failures/<timestamp>/` with `summary.json`, database snapshot, and captured stdout/stderr/log artifacts.

---

## JSON Expectation Format

Expectations are versioned JSON files.

```json
{
  "version": 1,
  "description": "Short URL is created with one analytics row",
  "tables": {
    "short_links": {
      "mode": "exact",
      "columns": ["short_code", "target_url", "created_by_user_id", "is_active"],
      "order_by": ["short_code"],
      "rows": [
        {
          "short_code": "abc123",
          "target_url": "https://example.com",
          "created_by_user_id": 1,
          "is_active": 1
        }
      ]
    },
    "analytics_events": {
      "mode": "contains",
      "columns": ["short_code", "event_type", "ip_address"],
      "ignore_columns": ["id", "created_at"],
      "rows": [
        {
          "short_code": "abc123",
          "event_type": "created",
          "ip_address": "127.0.0.1"
        }
      ]
    }
  }
}
```

### Table Spec Fields

| Field            | Required | Description                                                       |
|------------------|----------|-------------------------------------------------------------------|
| `mode`           | Yes      | `"exact"` or `"contains"` (see below).                           |
| `columns`        | No       | List of columns to select. All columns used if omitted.           |
| `ignore_columns` | No       | Columns to exclude from comparison (e.g., auto-generated IDs).   |
| `order_by`       | No       | Columns to order by in SQL. Python sort used if omitted.          |
| `rows`           | Yes      | List of expected row objects.                                     |

---

## Exact vs Contains Modes

### `exact`

The actual table rows must **exactly** match the expected rows after:
- Selecting only the requested columns
- Removing ignored columns
- Ordering rows deterministically

Extra rows are failures. Missing rows are failures. Different values are failures.

### `contains`

The expected rows must exist **somewhere** in the actual table result.

Extra actual rows are allowed. This is useful for large tables, logs, analytics events, and append-only histories.

---

## Ignored Columns

Use `ignore_columns` to skip columns that are auto-generated or non-deterministic:

```json
{
  "mode": "contains",
  "ignore_columns": ["id", "created_at"],
  "rows": [...]
}
```

This is equivalent to not including those columns in `columns`, but is explicit and self-documenting.

---

## Matchers

In addition to literal values, row fields can use matcher objects for flexible assertions:

```json
{
  "id": { "$any": true },
  "created_at": { "$not_null": true },
  "short_code": { "$regex": "^[a-zA-Z0-9_-]{6,16}$" },
  "visit_count": { "$gte": 1 },
  "target_url": { "$contains": "example.com" }
}
```

### Supported Matchers

| Matcher      | Description                                          |
|--------------|------------------------------------------------------|
| `$any`       | Any value accepted, including null.                  |
| `$not_null`  | Value must not be null.                              |
| `$regex`     | String value must match the regex pattern.           |
| `$contains`  | String value must contain the given substring.       |
| `$gt`        | Numeric value must be greater than operand.          |
| `$gte`       | Numeric value must be greater than or equal.         |
| `$lt`        | Numeric value must be less than operand.             |
| `$lte`       | Numeric value must be less than or equal.            |

---

## Output

### Passing Output (text mode)

```text
SQLite state assertion: PASS

Database: build/test.sqlite
Expectation: tests/integration/expected/create_short_url.json

Tables:
  short_links          PASS exact    expected=1 actual=1
  analytics_events     PASS contains expected=1 actual=3
```

### Failing Output (text mode)

```text
SQLite state assertion: FAIL

Database: build/test.sqlite
Expectation: tests/integration/expected/create_short_url.json

Tables:
  short_links          FAIL exact    expected=1 actual=1

Diff for table: short_links
  Rows differ

Expected:
[
  {
    "short_code": "abc123",
    "target_url": "https://example.com",
    "is_active": 1
  }
]

Actual:
[
  {
    "short_code": "abc123",
    "target_url": "https://wrong.example",
    "is_active": 1
  }
]

Unified diff:
--- expected
+++ actual
@@
-    "target_url": "https://example.com",
+    "target_url": "https://wrong.example",
```

### JSON Output Mode

```bash
python -m tools.sqlite_state_assert --db test.sqlite --expect expected.json --format json
```

```json
{
  "status": "fail",
  "database": "build/test.sqlite",
  "expectation": "tests/integration/expected/create_short_url.json",
  "tables": [
    {
      "name": "short_links",
      "status": "fail",
      "mode": "exact",
      "expected_count": 1,
      "actual_count": 1,
      "message": "Rows differ"
    }
  ]
}
```

---

## Example Integration-Test Workflow

```bash
# 1. Create clean database
python scripts/create_test_db.py --db build/test.sqlite

# 2. Run tested application actions
./url-shortener-cli create https://example.com --code abc123 --db build/test.sqlite
./url-shortener-cli resolve abc123 --ip 127.0.0.1 --db build/test.sqlite

# 3. Assert final database state
python -m tools.sqlite_state_assert \
  --db build/test.sqlite \
  --expect tests/integration/expected/create_and_resolve_short_url.json
```

---

## Running the Tests

Unit and integration tests can be run with:

```bash
python -m pytest tests/unit tests/integration
```

Or run just the tool-specific tests:

```bash
python -m pytest tests/unit/test_sqlite_state_assert_matchers.py \
                 tests/unit/test_sqlite_state_assert_compare.py \
                 tests/unit/test_sqlite_state_assert_json_validation.py \
                 tests/integration/test_sqlite_state_assert_cli.py \
                 tests/integration/test_sqlite_state_assert_runtime_cli.py
```

---

## SQL Injection Safety

The tool validates all table names, column names, and order-by columns against the pattern `^[A-Za-z_][A-Za-z0-9_]*$` before using them in SQL queries. Malformed identifiers cause an immediate exit with code `2`.

Example of a rejected table name:

```json
"users; DROP TABLE users": {}
```

This ensures that test fixture files cannot accidentally (or maliciously) execute arbitrary SQL.
