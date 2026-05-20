"""Unit tests for JSON expectation validation in sqlite_state_assert."""
import pytest
from tools.sqlite_state_assert import validate_expectation, load_expectation
import json
import tempfile
from pathlib import Path


def _valid_base():
    return {
        "version": 1,
        "tables": {
            "short_links": {
                "mode": "exact",
                "rows": [{"col": "val"}],
            }
        },
    }


class TestMissingVersion:
    def test_missing_version_raises(self):
        raw = {"tables": {"t": {"rows": []}}}
        with pytest.raises(ValueError, match="version"):
            validate_expectation(raw)


class TestUnsupportedVersion:
    def test_unsupported_version_raises(self):
        raw = {"version": 999, "tables": {}}
        with pytest.raises(ValueError, match="version"):
            validate_expectation(raw)


class TestMissingTables:
    def test_missing_tables_raises(self):
        raw = {"version": 1}
        with pytest.raises(ValueError, match="tables"):
            validate_expectation(raw)


class TestInvalidTableName:
    def test_invalid_table_name_raises(self):
        raw = {
            "version": 1,
            "tables": {
                "users; DROP TABLE users": {"rows": []},
            },
        }
        with pytest.raises(ValueError, match="identifier|Identifier"):
            validate_expectation(raw)

    def test_table_name_with_spaces_raises(self):
        raw = {"version": 1, "tables": {"my table": {"rows": []}}}
        with pytest.raises(ValueError):
            validate_expectation(raw)

    def test_table_name_starting_with_digit_raises(self):
        raw = {"version": 1, "tables": {"1table": {"rows": []}}}
        with pytest.raises(ValueError):
            validate_expectation(raw)


class TestInvalidColumnName:
    def test_invalid_column_name_raises(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["columns"] = ["valid_col", "bad col!"]
        with pytest.raises(ValueError):
            validate_expectation(raw)

    def test_invalid_order_by_raises(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["order_by"] = ["DROP TABLE"]
        with pytest.raises(ValueError):
            validate_expectation(raw)

    def test_invalid_ignore_columns_raises(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["ignore_columns"] = ["bad-col"]
        with pytest.raises(ValueError):
            validate_expectation(raw)


class TestUnsupportedMode:
    def test_unsupported_mode_raises(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["mode"] = "fuzzy"
        with pytest.raises(ValueError, match="mode"):
            validate_expectation(raw)


class TestRowsNotList:
    def test_rows_not_list_raises(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["rows"] = {"key": "value"}
        with pytest.raises(ValueError, match="rows"):
            validate_expectation(raw)


class TestRowNotObject:
    def test_row_not_object_raises(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["rows"] = ["not a dict"]
        with pytest.raises(ValueError, match=r"row"):
            validate_expectation(raw)


class TestLoadExpectation:
    def test_load_valid_json(self, tmp_path):
        data = _valid_base()
        p = tmp_path / "exp.json"
        p.write_text(json.dumps(data))
        result = load_expectation(p)
        assert result["version"] == 1

    def test_load_invalid_json_raises(self, tmp_path):
        p = tmp_path / "bad.json"
        p.write_text("{ not valid json }")
        with pytest.raises(ValueError, match="JSON"):
            load_expectation(p)

    def test_load_missing_file_raises(self, tmp_path):
        p = tmp_path / "missing.json"
        with pytest.raises(OSError):
            load_expectation(p)


class TestValidExpectation:
    def test_valid_exact_passes(self):
        validate_expectation(_valid_base())  # should not raise

    def test_valid_contains_passes(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["mode"] = "contains"
        validate_expectation(raw)  # should not raise

    def test_valid_with_columns_and_order_by(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["columns"] = ["short_code", "target_url"]
        raw["tables"]["short_links"]["order_by"] = ["short_code"]
        validate_expectation(raw)  # should not raise

    def test_valid_with_ignore_columns(self):
        raw = _valid_base()
        raw["tables"]["short_links"]["ignore_columns"] = ["id", "created_at"]
        validate_expectation(raw)  # should not raise
