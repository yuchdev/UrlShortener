"""Unit tests for value matchers in sqlite_state_assert."""
import pytest
from tools.sqlite_state_assert import _match_value, _apply_matcher


class TestLiteralEquality:
    def test_equal_strings(self):
        ok, msg = _match_value("abc", "abc")
        assert ok
        assert msg == ""

    def test_equal_integers(self):
        ok, _ = _match_value(42, 42)
        assert ok

    def test_equal_none(self):
        ok, _ = _match_value(None, None)
        assert ok

    def test_unequal_strings(self):
        ok, msg = _match_value("abc", "xyz")
        assert not ok
        assert "abc" in msg and "xyz" in msg

    def test_unequal_int_string(self):
        ok, _ = _match_value(1, "1")
        assert not ok


class TestMatcherAny:
    def test_any_matches_string(self):
        ok, _ = _match_value({"$any": True}, "anything")
        assert ok

    def test_any_matches_none(self):
        ok, _ = _match_value({"$any": True}, None)
        assert ok

    def test_any_matches_zero(self):
        ok, _ = _match_value({"$any": True}, 0)
        assert ok


class TestMatcherNotNull:
    def test_not_null_passes_for_value(self):
        ok, _ = _match_value({"$not_null": True}, "something")
        assert ok

    def test_not_null_fails_for_none(self):
        ok, msg = _match_value({"$not_null": True}, None)
        assert not ok
        assert "null" in msg.lower()

    def test_not_null_passes_for_zero(self):
        ok, _ = _match_value({"$not_null": True}, 0)
        assert ok


class TestMatcherRegex:
    def test_regex_match(self):
        ok, _ = _match_value({"$regex": r"^[a-z0-9]{6}$"}, "abc123")
        assert ok

    def test_regex_no_match(self):
        ok, msg = _match_value({"$regex": r"^[a-z0-9]{6}$"}, "UPPER")
        assert not ok
        assert "regex" in msg.lower()

    def test_regex_non_string(self):
        ok, msg = _match_value({"$regex": r"^\d+$"}, 123)
        assert not ok
        assert "string" in msg.lower()


class TestMatcherContains:
    def test_contains_substring(self):
        ok, _ = _match_value({"$contains": "example.com"}, "https://example.com/path")
        assert ok

    def test_contains_not_found(self):
        ok, msg = _match_value({"$contains": "missing"}, "https://example.com")
        assert not ok
        assert "missing" in msg

    def test_contains_non_string(self):
        ok, msg = _match_value({"$contains": "x"}, 42)
        assert not ok
        assert "string" in msg.lower()


class TestMatcherComparisons:
    def test_gt_passes(self):
        ok, _ = _match_value({"$gt": 5}, 6)
        assert ok

    def test_gt_fails_equal(self):
        ok, _ = _match_value({"$gt": 5}, 5)
        assert not ok

    def test_gt_fails_less(self):
        ok, _ = _match_value({"$gt": 5}, 4)
        assert not ok

    def test_gte_passes_equal(self):
        ok, _ = _match_value({"$gte": 5}, 5)
        assert ok

    def test_gte_passes_greater(self):
        ok, _ = _match_value({"$gte": 5}, 10)
        assert ok

    def test_gte_fails(self):
        ok, _ = _match_value({"$gte": 5}, 4)
        assert not ok

    def test_lt_passes(self):
        ok, _ = _match_value({"$lt": 10}, 5)
        assert ok

    def test_lt_fails_equal(self):
        ok, _ = _match_value({"$lt": 10}, 10)
        assert not ok

    def test_lte_passes_equal(self):
        ok, _ = _match_value({"$lte": 10}, 10)
        assert ok

    def test_lte_fails(self):
        ok, _ = _match_value({"$lte": 10}, 11)
        assert not ok

    def test_comparison_non_numeric(self):
        ok, msg = _match_value({"$gte": 1}, "text")
        assert not ok
        assert "numeric" in msg.lower()


class TestMatcherFailureMessages:
    def test_regex_message_includes_pattern(self):
        ok, msg = _match_value({"$regex": r"^\d+$"}, "abc")
        assert not ok
        assert r"^\d+$" in msg

    def test_gte_message_includes_operand(self):
        ok, msg = _match_value({"$gte": 100}, 5)
        assert not ok
        assert "100" in msg

    def test_contains_message_includes_substring(self):
        ok, msg = _match_value({"$contains": "needle"}, "haystack")
        assert not ok
        assert "needle" in msg


class TestUnsupportedMatcher:
    def test_unknown_matcher_returns_failure(self):
        ok, msg = _match_value({"$unknown_op": True}, "value")
        assert not ok
        assert "$unknown_op" in msg

    def test_unknown_matcher_via_apply_matcher(self):
        ok, msg = _apply_matcher("$bogus", None, "value")
        assert not ok
        assert "$bogus" in msg
