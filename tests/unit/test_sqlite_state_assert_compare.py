"""Unit tests for table comparison logic in sqlite_state_assert."""
import pytest
from tools.sqlite_state_assert import (
    TableExpectation,
    compare_table,
)


def _make_expectation(mode="exact", columns=None, ignore_columns=None, order_by=None, rows=None):
    return TableExpectation(
        name="test_table",
        mode=mode,
        columns=columns,
        ignore_columns=ignore_columns or [],
        order_by=order_by or [],
        rows=rows or [],
    )


class TestExactComparison:
    def test_exact_match_passes(self):
        exp = _make_expectation(
            mode="exact",
            rows=[{"a": 1, "b": "x"}, {"a": 2, "b": "y"}],
        )
        actual = [{"a": 1, "b": "x"}, {"a": 2, "b": "y"}]
        result = compare_table(exp, actual)
        assert result.status == "pass"
        assert result.diff is None

    def test_exact_match_fails_on_changed_value(self):
        exp = _make_expectation(
            mode="exact",
            rows=[{"a": 1, "b": "x"}],
        )
        actual = [{"a": 1, "b": "WRONG"}]
        result = compare_table(exp, actual)
        assert result.status == "fail"
        assert "differ" in result.message.lower()
        assert result.diff is not None

    def test_exact_match_fails_on_missing_row(self):
        exp = _make_expectation(
            mode="exact",
            rows=[{"a": 1}, {"a": 2}],
        )
        actual = [{"a": 1}]
        result = compare_table(exp, actual)
        assert result.status == "fail"
        assert result.expected_count == 2
        assert result.actual_count == 1

    def test_exact_match_fails_on_extra_row(self):
        exp = _make_expectation(
            mode="exact",
            rows=[{"a": 1}],
        )
        actual = [{"a": 1}, {"a": 2}]
        result = compare_table(exp, actual)
        assert result.status == "fail"
        assert result.expected_count == 1
        assert result.actual_count == 2

    def test_exact_empty_both_passes(self):
        exp = _make_expectation(mode="exact", rows=[])
        result = compare_table(exp, [])
        assert result.status == "pass"

    def test_exact_diff_is_unified_diff(self):
        exp = _make_expectation(mode="exact", rows=[{"url": "https://example.com"}])
        actual = [{"url": "https://wrong.com"}]
        result = compare_table(exp, actual)
        assert result.diff is not None
        assert "example.com" in result.diff or "wrong.com" in result.diff


class TestContainsComparison:
    def test_contains_passes_exact_match(self):
        exp = _make_expectation(
            mode="contains",
            rows=[{"a": 1}],
        )
        actual = [{"a": 1}]
        result = compare_table(exp, actual)
        assert result.status == "pass"

    def test_contains_passes_with_extra_actual_rows(self):
        exp = _make_expectation(
            mode="contains",
            rows=[{"a": 1}],
        )
        actual = [{"a": 1}, {"a": 2}, {"a": 3}]
        result = compare_table(exp, actual)
        assert result.status == "pass"

    def test_contains_fails_when_expected_row_missing(self):
        exp = _make_expectation(
            mode="contains",
            rows=[{"a": 999}],
        )
        actual = [{"a": 1}, {"a": 2}]
        result = compare_table(exp, actual)
        assert result.status == "fail"
        assert "not found" in result.message.lower()

    def test_contains_partial_match_fails(self):
        exp = _make_expectation(
            mode="contains",
            rows=[{"a": 1, "b": "expected"}],
        )
        actual = [{"a": 1, "b": "wrong"}]
        result = compare_table(exp, actual)
        assert result.status == "fail"


class TestDeterministicSorting:
    def test_sort_without_order_by_is_deterministic(self):
        exp = _make_expectation(
            mode="exact",
            order_by=[],
            rows=[{"a": 2}, {"a": 1}],
        )
        # Actual in reversed order - without order_by, both are sorted by JSON representation
        actual = [{"a": 1}, {"a": 2}]
        result = compare_table(exp, actual)
        assert result.status == "pass"

    def test_multiple_runs_same_result(self):
        exp = _make_expectation(
            mode="exact",
            rows=[{"z": 3}, {"a": 1}, {"m": 2}],
        )
        actual = [{"z": 3}, {"m": 2}, {"a": 1}]
        r1 = compare_table(exp, actual)
        r2 = compare_table(exp, actual)
        assert r1.status == r2.status


class TestIgnoredColumns:
    def test_ignored_columns_are_not_compared(self):
        exp = _make_expectation(
            mode="exact",
            ignore_columns=["id", "created_at"],
            rows=[{"name": "alice"}],
        )
        # actual has extra columns that should be ignored
        actual = [{"id": 999, "name": "alice", "created_at": "2099-01-01"}]
        result = compare_table(exp, actual)
        assert result.status == "pass"

    def test_difference_in_non_ignored_col_still_fails(self):
        exp = _make_expectation(
            mode="exact",
            ignore_columns=["id"],
            rows=[{"name": "alice"}],
        )
        actual = [{"id": 1, "name": "bob"}]
        result = compare_table(exp, actual)
        assert result.status == "fail"

    def test_contains_with_ignored_columns_passes(self):
        exp = _make_expectation(
            mode="contains",
            ignore_columns=["created_at"],
            rows=[{"code": "abc"}],
        )
        actual = [{"code": "abc", "created_at": "2024-01-01"}]
        result = compare_table(exp, actual)
        assert result.status == "pass"
