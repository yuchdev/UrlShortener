#!/usr/bin/env python3
"""20_coverage_gate.py - unittest wrapper around tools/conformance_coverage.py.

This is the CTest-registered face (``conformance_20_coverage_gate``, labels
``integration;conformance``) of the corpus coverage gate. It proves:

1. The **real** corpus fully covers the **live** registry: ``main()`` exits 0
   against the built binary, the real catalog and the real corpus, with only the
   reviewed error-code waivers.
2. Each coverage check fails loudly on a synthetic gap: a command with no case, a
   catalog error code neither covered nor waived, an uncovered resource route, a
   stale waiver (evidence file missing), an orphan case, and a mutating command
   with no ``authz_matrix`` case.
3. The matrix report surfaces counted skip-notes (``lsof`` / ``netstat``
   unavailable, the carry-forward from subtask 21) as a reported number.

The test resolves every path from its own ``__file__`` so it runs identically
from any working directory (CTest runs it from the build tree) and on Windows. It
runs after the full conformance suite (CTest ``DEPENDS``) so the per-transport
summaries the matrix reads exist; the pass/fail verdict itself needs no summary.
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Dict, List, Optional

# Resolve the repository root from this file (tests/conformance/py/<this>), never
# from the process cwd, then make ``tools`` importable (same shape as the lint
# wrapper 00_corpus_lint.py).
_THIS = Path(__file__).resolve()
_REPO_ROOT = _THIS.parents[3]
if str(_REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(_REPO_ROOT))

from tools import conformance_coverage as covg  # noqa: E402

#: Real corpus directory and catalog replayed by every transport.
_CORPUS_DIR = _REPO_ROOT / "tests" / "conformance" / "commands"
_CATALOG = _REPO_ROOT / "docs" / "commands" / "README.md"
_WAIVERS = _REPO_ROOT / "tools" / "conformance_waivers.json"


def _descriptor(
    name: str,
    *,
    args: Optional[List[Dict[str, Any]]] = None,
    mutating: bool = False,
    dry_run: bool = False,
    perm: str = "read",
) -> Dict[str, Any]:
    """Build a minimal ``command.list`` descriptor for a synthetic registry."""
    return {
        "name": name,
        "args": args or [],
        "mutating": mutating,
        "dry_run": dry_run,
        "perm": perm,
    }


def _write_corpus(tmp: Path, cases: List[Dict[str, Any]]) -> Path:
    """Write *cases* into a one-file corpus under *tmp* and return its directory."""
    corpus_dir = tmp / "commands"
    corpus_dir.mkdir(parents=True, exist_ok=True)
    (corpus_dir / "00_synth.json").write_text(
        json.dumps({"corpus_version": 1, "suite": "synth", "cases": cases}),
        encoding="utf-8",
    )
    return corpus_dir


def _success_case(name: str, command: str) -> Dict[str, Any]:
    """A case whose single step runs *command* and expects success."""
    return {
        "name": name,
        "doc": "synthetic success",
        "command": command,
        "steps": [
            {
                "request": {"v": 1, "command": command, "args": {}},
                "expect": {"ok": True, "result": {}, "match": "subset"},
            }
        ],
    }


class CoverageGateRealCorpusTest(unittest.TestCase):
    """The real corpus fully covers the live registry (the headline gate)."""

    def test_real_corpus_passes(self) -> None:
        """``main()`` exits 0 on the binary, real catalog and real corpus.

        Type: integration (end-to-end gate).
        Scenario: the committed corpus covers every registry command and every
          catalog error code (modulo the three reviewed waivers).
        Boundaries: this is the live coupling to the built binary's registry.
        On failure first check: was a command or error code added without a
          corpus case, or did a waiver's evidence file move?
        """
        binary = covg._find_binary_fallback()
        self.assertIsNotNone(
            binary, "no url_shortener binary; set SIMPLE_HTTP_BIN or build it"
        )
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "matrix.md"
            rc = covg.main(
                [
                    "--binary", str(binary),
                    "--catalog", str(_CATALOG),
                    "--corpus", str(_CORPUS_DIR),
                    "--waivers", str(_WAIVERS),
                    "--report", str(report),
                ]
            )
            self.assertEqual(rc, 0, "real corpus coverage gate failed")
            self.assertTrue(report.is_file(), "report was not written")

    def test_waivers_file_is_valid_and_minimal(self) -> None:
        """Every committed waiver is well-formed and its evidence file exists.

        Type: integration (waiver policy).
        Scenario: the three reviewed waivers (storage_failure, internal, busy)
          each carry an owner, a reason and an existing evidence path.
        On failure first check: did a waived code's covering test move or get
          renamed?
        """
        waivers = covg.load_waivers(_WAIVERS)
        self.assertEqual(
            set(waivers), {"storage_failure", "internal", "busy"},
            "unexpected waiver set; new waivers need reviewer approval",
        )
        for code, waiver in waivers.items():
            self.assertTrue(waiver.owner, f"waiver {code} has no owner")
            self.assertTrue(waiver.reason, f"waiver {code} has no reason")
            self.assertTrue(
                (_REPO_ROOT / waiver.evidence).is_file(),
                f"waiver {code} evidence {waiver.evidence} missing",
            )


class CoverageGateSyntheticTest(unittest.TestCase):
    """Each check fails on a crafted gap, using a fake registry and corpus."""

    def _check(
        self,
        registry: List[Dict[str, Any]],
        cases: List[Dict[str, Any]],
        *,
        codes: Optional[set] = None,
        routes: Optional[list] = None,
        waivers: Optional[Dict[str, covg.Waiver]] = None,
        documented: Optional[Dict[str, set]] = None,
    ) -> covg.CheckResult:
        """Index *cases* and run :func:`conformance_coverage.check` on them."""
        with tempfile.TemporaryDirectory() as tmp:
            corpus_dir = _write_corpus(Path(tmp), cases)
            index = covg.corpus_index(corpus_dir)
        return covg.check(
            index,
            registry,
            codes if codes is not None else set(),
            routes if routes is not None else [],
            waivers if waivers is not None else {},
            documented if documented is not None else {},
        )

    def _categories(self, result: covg.CheckResult) -> set:
        return {f.category for f in result.findings}

    def test_missing_command_is_a_finding(self) -> None:
        """A registry command with no corpus case yields a ``command`` finding."""
        result = self._check(
            [_descriptor("extra.cmd")],
            [_success_case("some_other", "link.get")],
        )
        self.assertIn("command", self._categories(result))

    def test_missing_error_code_is_a_finding(self) -> None:
        """A catalog code neither covered nor waived yields an ``error_code``."""
        result = self._check(
            [_descriptor("link.get")],
            [_success_case("ok_case", "link.get")],
            codes={"not_found"},
        )
        self.assertIn("error_code", self._categories(result))

    def test_waived_error_code_is_not_a_finding(self) -> None:
        """A valid waiver (owner, reason, existing evidence) clears the code."""
        waiver = covg.Waiver(
            "not_found",
            owner="tester",
            reason="synthetic",
            evidence="tools/conformance_coverage.py",
        )
        result = self._check(
            [_descriptor("link.get")],
            [_success_case("ok_case", "link.get")],
            codes={"not_found"},
            waivers={"not_found": waiver},
        )
        self.assertNotIn("error_code", self._categories(result))
        self.assertIn("not_found", result.waived_codes)

    def test_stale_waiver_file_is_a_finding(self) -> None:
        """A waiver whose evidence path does not exist yields a ``waiver``."""
        waiver = covg.Waiver(
            "not_found",
            owner="tester",
            reason="synthetic",
            evidence="tools/does_not_exist_zzz.cpp",
        )
        result = self._check(
            [_descriptor("link.get")],
            [_success_case("ok_case", "link.get")],
            codes={"not_found"},
            waivers={"not_found": waiver},
        )
        self.assertIn("waiver", self._categories(result))

    def test_missing_route_is_a_finding(self) -> None:
        """A section-5 link resource route with no rest_resource step fails."""
        result = self._check(
            [_descriptor("link.get")],
            [_success_case("ok_case", "link.get")],
            routes=[("GET", "/api/v1/links/{slug}")],
        )
        self.assertIn("route", self._categories(result))

    def test_generic_route_without_step_is_a_note_not_a_finding(self) -> None:
        """A gated ``/api/v1/jobs*`` route with no rest_resource step is a note."""
        result = self._check(
            [_descriptor("job.get")],
            [_success_case("ok_case", "job.get")],
            routes=[("GET", "/api/v1/jobs/{id}")],
        )
        self.assertNotIn("route", self._categories(result))
        self.assertTrue(result.route_notes, "generic route should be a note")

    def test_orphan_case_is_a_finding(self) -> None:
        """A case whose command is absent from the registry is an ``orphan``."""
        result = self._check(
            [_descriptor("link.get")],
            [_success_case("stale_case", "gone.command")],
        )
        self.assertIn("orphan", self._categories(result))

    def test_unknown_command_case_is_not_an_orphan(self) -> None:
        """A case expecting ``unknown_command`` is exempt from the orphan check."""
        case = {
            "name": "deliberately_unknown",
            "doc": "unknown command probe",
            "command": "nope.nothing",
            "steps": [
                {
                    "request": {"v": 1, "command": "nope.nothing", "args": {}},
                    "expect": {"ok": False, "error": {"code": "unknown_command"}},
                }
            ],
        }
        result = self._check([_descriptor("link.get")], [case])
        self.assertNotIn("orphan", self._categories(result))

    def test_mutating_command_without_authz_matrix_is_a_finding(self) -> None:
        """A mutating command with no ``authz_matrix_<cmd>`` case fails check 4."""
        result = self._check(
            [_descriptor("link.delete", mutating=True, perm="write",
                         args=[{"name": "slug", "type": "string", "required": True}])],
            [
                _success_case("del_ok", "link.delete"),
                {
                    "name": "del_fail",
                    "doc": "a failure so check 1 is satisfied",
                    "command": "link.delete",
                    "steps": [
                        {
                            "request": {"v": 1, "command": "link.delete",
                                        "args": {"slug": "x"}},
                            "expect": {"ok": False, "error": {"code": "not_found"}},
                        }
                    ],
                },
            ],
            documented={"link.delete": {"not_found"}},
        )
        self.assertIn("mutating", self._categories(result))

    def test_dry_run_descriptor_without_option_is_a_finding(self) -> None:
        """A ``dry_run: true`` descriptor with no options.dry_run step fails."""
        result = self._check(
            [_descriptor("link.create", dry_run=True, mutating=True, perm="write")],
            [_success_case("create_ok", "link.create")],
        )
        self.assertIn("dry_run", self._categories(result))


class CoverageGateCatalogParsingTest(unittest.TestCase):
    """The catalog parsers read the real catalog as the gate expects."""

    def test_section5_routes_are_parsed(self) -> None:
        """Section 5 yields the link resource routes plus the generic rows."""
        routes = covg.catalog_routes(_CATALOG)
        self.assertIn(("POST", "/api/v1/links"), routes)
        self.assertIn(("GET", "/api/v1/links/{slug}"), routes)
        self.assertIn(("POST", "/api/v1/links/{slug}/disable"), routes)
        self.assertIn(("POST", "/api/v1/links/{slug}/restore"), routes)
        self.assertIn(("POST", "/api/v1/jobs/{id}/cancel"), routes)

    def test_documented_errors_are_parsed_including_inline_job_commands(self) -> None:
        """Every ``Errors:`` line is attributed, by heading or inline command.

        ``link.create`` / ``link.get`` / ``link.update`` and the read commands that
        gained an ``Errors:`` line (``db.rows``, ``link.stats``, ``link.preview``,
        ``link.list``, ``link.export``) are keyed by their ``####`` heading;
        ``job.get`` / ``job.cancel`` live in a table with no heading and are keyed
        by the dotted command backticked inline before ``Errors:``.
        ``link.delete`` is keyed by its single-command ``####`` heading, while
        ``link.enable`` / ``link.disable`` / ``link.restore`` share one combined
        heading (whose regex captures only the first command), so ``disable`` and
        ``restore`` rely on the dotted command backticked inline before
        ``Errors:``.
        """
        documented = covg.parse_documented_errors(_CATALOG)
        self.assertEqual(
            set(documented),
            {
                "link.create", "link.get", "link.update", "db.rows",
                "link.stats", "link.preview", "link.list", "link.export",
                "job.get", "job.cancel",
                "link.delete", "link.enable", "link.disable", "link.restore",
            },
        )
        self.assertIn("not_found", documented["link.get"])
        self.assertIn("not_found", documented["link.preview"])
        # Inline attribution: the backticked `app::UpdateLinkCommand` token before
        # link.update's Errors: line must NOT hijack its codes.
        self.assertEqual(documented["job.cancel"], {"not_found", "not_authorized"})
        self.assertEqual(documented["job.get"], {"not_found"})
        # Combined-heading inline attribution: disable/restore are not the first
        # command in their shared heading, so they are only attributed via the
        # inline backticked command before Errors:.
        self.assertEqual(
            documented["link.disable"], {"not_found", "storage_failure"})
        self.assertEqual(
            documented["link.restore"], {"not_found", "storage_failure"})


class CoverageMatrixAndSkipNotesTest(unittest.TestCase):
    """The matrix report renders counts and surfaces counted skip-notes."""

    def _index_two_commands(self) -> covg.CorpusIndex:
        cases = [
            _success_case("c1", "link.get"),
            _success_case("c2", "link.get"),
            _success_case("c3", "system.info"),
        ]
        with tempfile.TemporaryDirectory() as tmp:
            return covg.corpus_index(_write_corpus(Path(tmp), cases))

    def test_skip_notes_are_counted_from_both_summary_shapes(self) -> None:
        """``lsof``/``netstat``/``unavailable`` skip reasons are counted.

        Type: unit (reduced-coverage surfacing, carry-forward from subtask 21).
        Scenario: a Python ``_SummaryCollector`` summary and a C++ ``RunReport``
          summary each carry a skip whose reason names an unavailable enumeration
          tool; the gate reports the per-transport count.
        On failure first check: did the summary shape or the skip-note tokens
          change?
        """
        summaries = {
            "stdio": {
                "transport": "stdio",
                "cases": [
                    {"name": "a", "command": "link.get", "status": "pass"},
                    {"name": "b", "command": "link.get", "status": "skip",
                     "reason": "no working lsof; port-probe fallback"},
                ],
            },
            "direct": {
                "transport": "direct",
                "ran": 3,
                "skipped": [
                    {"case": "c", "reason": "netstat unavailable on this host"},
                ],
            },
        }
        counts = covg.count_skip_notes(summaries)
        self.assertEqual(counts.get("stdio"), 1)
        self.assertEqual(counts.get("direct"), 1)

    def test_matrix_reports_ran_skip_counts_and_notes(self) -> None:
        """The matrix renders ran/skip cells and a skip-note footer line."""
        index = self._index_two_commands()
        commands = [_descriptor("link.get"), _descriptor("system.info")]
        summaries = {
            "cli_cmd": {
                "transport": "cli_cmd",
                "cases": [
                    {"name": "c1", "command": "link.get", "status": "pass"},
                    {"name": "c2", "command": "link.get", "status": "skip",
                     "reason": "cli_cmd cannot share an in-memory backend"},
                    {"name": "c3", "command": "system.info", "status": "pass"},
                ],
            },
        }
        matrix = covg.build_matrix(index, commands, summaries)
        self.assertIn("| `link.get` |", matrix)
        self.assertIn("1/1", matrix)  # link.get: 1 ran, 1 skipped on cli_cmd
        self.assertIn("Counted skip-notes", matrix)


if __name__ == "__main__":
    unittest.main()
