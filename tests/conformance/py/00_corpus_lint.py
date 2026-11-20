#!/usr/bin/env python3
"""00_corpus_lint.py - unittest wrapper around tools/conformance_lint.py.

This is the CTest-registered face (``conformance_00_corpus_lint``, labels
``integration;conformance``) of the corpus linter. It proves three things:

1. The real corpus (``tests/conformance/commands/*.json``) and the schema
   self-test corpus lint clean.
2. Every numbered lint rule has a negative fixture under ``lint_negative/`` and
   that fixture yields *exactly* its own rule id (no more, no less) - so a rule
   can never silently stop firing.
3. The linter never imports or runs the product binary (it is pure stdlib).

The test resolves every path from its own ``__file__`` so it runs identically
from any working directory (CTest runs it from the build tree) and on Windows
(no ``/proc``, no POSIX-only assumptions).
"""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path

# Resolve the repository root from this file (tests/conformance/py/<this>),
# never from the process cwd, then make ``tools`` importable.
_THIS = Path(__file__).resolve()
_REPO_ROOT = _THIS.parents[3]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools.conformance_lint import Finding, lint_paths, main  # noqa: E402

#: Real corpus directory (data replayed by every later shim).
CORPUS_DIR = _REPO_ROOT / "tests" / "conformance" / "commands"

#: Schema self-test corpus (sets ``allow_unknown_codes``).
SELFTEST_CORPUS = CORPUS_DIR / "00_schema_selftest.json"

#: Directory of one deliberately-broken corpus per lint rule.
NEGATIVE_DIR = _THIS.parent / "lint_negative"

#: Fixture filenames encode their expected rule: ``rule_NN_<desc>.json``.
_FIXTURE_RE = re.compile(r"^rule_(\d{2})_.*\.json$")

#: Total number of lint rules that must each own a fixture.
EXPECTED_RULE_COUNT = 12


class CorpusLintTest(unittest.TestCase):
    """Verify the conformance-corpus linter against the real corpus and
    against one negative fixture per rule."""

    def test_real_corpus_is_clean(self) -> None:
        """Every file in ``tests/conformance/commands`` lints with zero
        findings."""
        corpus = sorted(CORPUS_DIR.glob("*.json"))
        self.assertTrue(corpus, f"no corpus files found under {CORPUS_DIR}")
        findings = lint_paths(corpus)
        self.assertEqual(
            findings,
            [],
            "real corpus is not clean:\n"
            + "\n".join(f.format() for f in findings),
        )

    def test_selftest_corpus_is_clean(self) -> None:
        """The schema self-test corpus lints clean on its own."""
        self.assertTrue(
            SELFTEST_CORPUS.exists(), f"missing {SELFTEST_CORPUS}"
        )
        findings = lint_paths([SELFTEST_CORPUS])
        self.assertEqual(
            findings,
            [],
            "self-test corpus is not clean:\n"
            + "\n".join(f.format() for f in findings),
        )

    def test_every_rule_has_a_fixture(self) -> None:
        """All twelve rules are represented by exactly one fixture file."""
        fixtures = sorted(NEGATIVE_DIR.glob("rule_*.json"))
        rule_ids = set()
        for fixture in fixtures:
            match = _FIXTURE_RE.match(fixture.name)
            self.assertIsNotNone(
                match, f"fixture {fixture.name} does not match rule_NN_*.json"
            )
            rule_ids.add(int(match.group(1)))
        self.assertEqual(
            rule_ids,
            set(range(1, EXPECTED_RULE_COUNT + 1)),
            "fixtures do not cover exactly rules 1..12",
        )

    def test_each_fixture_yields_only_its_rule(self) -> None:
        """Each negative fixture produces findings for its rule and no other.

        A fixture that tripped a second rule would make the single-rule
        guarantee meaningless, so this asserts the finding set is exactly the
        one rule encoded in the filename.
        """
        for fixture in sorted(NEGATIVE_DIR.glob("rule_*.json")):
            with self.subTest(fixture=fixture.name):
                expected = int(_FIXTURE_RE.match(fixture.name).group(1))
                findings = lint_paths([fixture])
                self.assertTrue(
                    findings, f"{fixture.name} produced no findings"
                )
                observed = {f.rule for f in findings}
                self.assertEqual(
                    observed,
                    {expected},
                    f"{fixture.name} should yield only RULE-{expected}, got "
                    + ", ".join(f.format() for f in findings),
                )

    def test_main_returns_nonzero_on_broken_fixture(self) -> None:
        """``main`` exits 1 on a broken fixture and 0 on the clean corpus."""
        self.assertEqual(main([str(SELFTEST_CORPUS)]), 0)
        broken = NEGATIVE_DIR / "rule_01_unknown_key.json"
        self.assertEqual(main([str(broken)]), 1)

    def test_main_accepts_a_directory(self) -> None:
        """``main`` globs a directory argument (the PowerShell-safe form)."""
        self.assertEqual(main([str(CORPUS_DIR)]), 0)


if __name__ == "__main__":
    unittest.main()
