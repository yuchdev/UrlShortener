#!/usr/bin/env python3
"""01_matcher_vectors.py - run the shared matcher/normalizer vectors.

This is the CTest-registered face (``conformance_01_matcher_vectors``, labels
``integration;conformance``) of the Python command-corpus matcher. It replays
every vector in ``tests/conformance/normalizer_vectors.json`` - the single
normative definition of how an actual envelope is normalized and matched against
an ``expect`` block - through :mod:`conformance_match`, one ``subTest`` per
vector. The C++ runner (Task 07.0 subtask 03) consumes the same file unchanged,
so the two matchers cannot drift.

Every vector carries its own expected verdict (``match``) and, optionally, a
substring that the failure reason must contain (``reason_contains``). A vector
with ``normalize: true`` is first run through ``normalize_actual`` (rules
N1/N4/N5); the resulting findings join the match reasons so that an N4 id
mismatch or an N5 extra-key violation fails the vector even in ``subset`` mode.

The test resolves every path from its own ``__file__`` and inserts its own
directory onto ``sys.path`` so it imports ``conformance_match`` and runs
identically under ``python3 -I`` (isolated mode) from any working directory.
"""

from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path

# Resolve paths from this file, never the process cwd, and make the sibling
# ``conformance_match`` module importable under ``python3 -I``.
_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_match as cm  # noqa: E402

#: The shared vector file (tests/conformance/normalizer_vectors.json).
VECTORS_PATH = _THIS.parents[1] / "normalizer_vectors.json"

#: Minimum number of vectors the file must carry (spec 07.0/02 success
#: criterion: at least 60).
MIN_VECTORS = 60


def _load_vectors() -> list:
    """Load and return the vector list from :data:`VECTORS_PATH`."""
    with VECTORS_PATH.open(encoding="utf-8") as handle:
        return json.load(handle)


def _run_vector(vector: dict) -> list:
    """Evaluate one vector and return the list of mismatch reasons.

    Applies ``${var}`` substitution to ``expected`` (an unknown-variable error
    becomes a reason), optionally normalizes ``actual`` (N1/N4/N5), then matches.
    An empty list means the vector matched.

    :param vector: One entry of the vector file.
    :return: ``[]`` when the vector matches, else one reason string per issue.
    """
    variables = vector.get("variables", {})
    mode = vector.get("mode", "exact")
    try:
        expected = cm.substitute(vector["expected"], variables)
        actual = vector["actual"]
        findings: list = []
        if vector.get("normalize"):
            actual, findings = cm.normalize_actual(
                actual, vector.get("request"), vector.get("carries_id", False)
            )
        return findings + cm.match(expected, actual, mode, variables)
    except ValueError as exc:
        return [f"$: substitution error: {exc}"]
    except RecursionError:
        # CWE-674: a pathologically deep input must become a clean mismatch
        # finding, never an uncaught error that aborts the whole suite.
        return ["$: input nested too deeply to evaluate"]


class MatcherVectorTest(unittest.TestCase):
    """Replay every shared matcher/normalizer vector and verify its verdict."""

    def test_vector_file_is_well_formed(self) -> None:
        """The vector file exists, has at least 60 vectors with unique names."""
        self.assertTrue(VECTORS_PATH.exists(), f"missing {VECTORS_PATH}")
        vectors = _load_vectors()
        self.assertIsInstance(vectors, list)
        self.assertGreaterEqual(
            len(vectors),
            MIN_VECTORS,
            f"expected >= {MIN_VECTORS} vectors, found {len(vectors)}",
        )
        names = [v["name"] for v in vectors]
        self.assertEqual(
            len(names), len(set(names)), "vector names must be unique"
        )

    def test_every_vector_matches_its_expectation(self) -> None:
        """Each vector yields exactly the verdict (and reason) it declares."""
        for vector in _load_vectors():
            with self.subTest(vector=vector["name"]):
                reasons = _run_vector(vector)
                matched = not reasons
                self.assertEqual(
                    matched,
                    vector["match"],
                    f"{vector['name']}: expected match={vector['match']}, "
                    f"got reasons={reasons}",
                )
                if "reason_contains" in vector:
                    needle = vector["reason_contains"]
                    self.assertTrue(
                        any(needle in reason for reason in reasons),
                        f"{vector['name']}: no reason contains {needle!r}; "
                        f"reasons={reasons}",
                    )

    def test_forbidden_regex_tokens_are_rejection_vectors(self) -> None:
        """A vector whose ``<regex:...>`` token is outside the shared dialect
        must declare ``match: false`` - the matcher is required to refuse such a
        pattern (dialect drift or ReDoS), never execute it and let it pass.
        """
        for vector in _load_vectors():
            for value in _iter_strings(vector.get("expected")):
                token = cm._parse_token(value)
                if token and token[0] == "regex" and cm.forbidden_regex_reason(
                    token[1]
                ):
                    with self.subTest(vector=vector["name"], pattern=token[1]):
                        self.assertFalse(
                            vector["match"],
                            f"{vector['name']}: forbidden regex {token[1]!r} "
                            "must be a rejection (match: false) vector",
                        )

    def test_regex_dialect_guard(self) -> None:
        """``forbidden_regex_reason`` accepts the shared dialect and rejects
        both dialect mismatches and catastrophic-backtracking patterns."""
        for safe in ("[a-z]+", "[0-9]{3}", "a.c", "(ab)+", "(?:ab)c", "a|b"):
            self.assertIsNone(
                cm.forbidden_regex_reason(safe), f"{safe!r} should be allowed"
            )
        for bad in (
            "(a+)+",       # nested quantifier
            "(a*)*",       # nested quantifier
            "(.*a){20}",   # quantified group with inner quantifier
            "(a|a)+",      # quantified alternation
            "(?<=x)y",     # lookbehind (dialect)
            "a*+",         # possessive (dialect)
        ):
            self.assertIsNotNone(
                cm.forbidden_regex_reason(bad), f"{bad!r} should be rejected"
            )

    def test_matcher_refuses_catastrophic_regex_without_running_it(self) -> None:
        """``<regex:(a+)+>`` is refused as a finding before ``re.fullmatch`` ever
        runs, so a ReDoS input cannot hang the matcher (CWE-1333)."""
        reason = cm._match_token("regex", "(a+)+", "a" * 40 + "!", {})
        self.assertIsNotNone(reason)
        self.assertIn("disallowed", reason)

    def test_deeply_nested_input_yields_clean_finding(self) -> None:
        """An approximately 100k-deep nested structure becomes a clean mismatch
        finding, not an uncaught ``RecursionError`` (CWE-674).

        Both the ``normalize_actual`` deepcopy path and the ``match`` recursion
        path are exercised.
        """
        depth = 100_000

        def deep() -> dict:
            root: dict = {}
            node = root
            for _ in range(depth):
                child: dict = {}
                node["n"] = child
                node = child
            return root

        normalize_vector = {
            "name": "deep_normalize",
            "mode": "exact",
            "normalize": True,
            "expected": {"v": 1, "ok": True, "result": {}},
            "actual": {"v": 1, "ok": True, "result": deep()},
            "match": False,
        }
        match_vector = {
            "name": "deep_match",
            "mode": "exact",
            "expected": {"root": deep()},
            "actual": {"root": deep()},
            "match": False,
        }
        for vector in (normalize_vector, match_vector):
            with self.subTest(vector=vector["name"]):
                reasons = _run_vector(vector)
                self.assertTrue(reasons, "expected a non-empty finding list")
                self.assertTrue(
                    any("nested too deeply" in reason for reason in reasons),
                    f"expected a depth finding, got {reasons}",
                )

    def test_substitute_raises_on_unknown_variable(self) -> None:
        """``substitute`` rejects a reference to an unsaved variable."""
        with self.assertRaises(ValueError):
            cm.substitute({"x": "${nope}"}, {})

    def test_jsonpath_get_reads_nested_values(self) -> None:
        """``jsonpath_get`` resolves ``$.a.b[0].c`` and raises on absence."""
        doc = {"result": {"items": [{"slug": "docs"}, {"slug": "api"}]}}
        self.assertEqual(
            cm.jsonpath_get(doc, "$.result.items[1].slug"), "api"
        )
        self.assertEqual(cm.jsonpath_get(doc, "$"), doc)
        with self.assertRaises(KeyError):
            cm.jsonpath_get(doc, "$.result.missing")
        with self.assertRaises(IndexError):
            cm.jsonpath_get(doc, "$.result.items[9].slug")

    def test_jsonpath_get_rejects_wildcards_and_filters(self) -> None:
        """``jsonpath_get`` refuses wildcards, recursive descent and filters."""
        doc = {"a": [1, 2, 3]}
        for bad in ("a.b", "$.a[*]", "$..a", "$.a[?(@.x)]"):
            with self.subTest(path=bad):
                with self.assertRaises(ValueError):
                    cm.jsonpath_get(doc, bad)


def _iter_strings(node) -> list:
    """Return every string value found anywhere inside *node*."""
    found: list = []
    stack = [node]
    while stack:
        current = stack.pop()
        if isinstance(current, str):
            found.append(current)
        elif isinstance(current, dict):
            stack.extend(current.values())
        elif isinstance(current, list):
            stack.extend(current)
    return found


if __name__ == "__main__":
    unittest.main()
