#!/usr/bin/env python3
"""02_harness_selftest.py - self-test for the conformance harness common module.

Drives :func:`conformance_common.run_case` with a :class:`FakeAdapter` that
echoes canned envelopes, so the harness is exercised end to end without the real
binary (the shims 06-09 add the real transports). The eight cases below cover the
behaviours later shims rely on:

1. ``test_pass`` - a matching envelope yields no findings.
2. ``test_mismatch_reports_path`` - a wrong field is reported with its JSONPath.
3. ``test_explicit_skip`` - a ``transports.skip`` entry raises ``SkipTest``.
4. ``test_implicit_capability_skip_is_failure`` - an undeclared capability gap is
   a failure, not a silent skip.
5. ``test_job_poll_success`` - ``await_job`` polls to a terminal success.
6. ``test_job_poll_timeout`` - a job that never terminates yields a finding.
7. ``test_variable_save_and_substitute`` - ``save`` then ``${var}`` round-trips.
8. ``test_tmp_windows_path_normalized`` - ``${tmp}`` expands with ``/`` separators
   even from a Windows-style temp path.

A ninth test exercises :func:`build_suite` and the coverage-summary writer.

The file resolves its sibling import from ``__file__`` so it runs identically
under ``python3 -I`` (CTest registers it that way).
"""

from __future__ import annotations

import copy
import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any, Dict, List, Optional

_THIS = Path(__file__).resolve()
if str(_THIS.parent) not in sys.path:
    sys.path.insert(0, str(_THIS.parent))

import conformance_common as cc  # noqa: E402


# ---------------------------------------------------------------------------
# Canned envelopes
# ---------------------------------------------------------------------------

OK_CREATE = {
    "v": 1,
    "ok": True,
    "id": "srv-1",
    "result": {"slug": "docs", "created_at": "2026-01-01T00:00:00Z", "id": "docs"},
}

OK_GET = {
    "v": 1,
    "ok": True,
    "id": "srv-2",
    "result": {"slug": "docs", "id": "docs"},
}

OK_EMPTY = {"v": 1, "ok": True, "id": "srv-1", "result": {}}

ACCEPT = {"v": 1, "ok": True, "id": "srv-1", "job": {"id": "j-1", "state": "queued"}}

JOB_RUNNING = {
    "v": 1,
    "ok": True,
    "id": "srv-2",
    "result": {
        "id": "j-1",
        "command": "link.import",
        "state": "running",
        "progress": {"done": 0, "total": 1},
        "created_at": "2026-01-01T00:00:00Z",
    },
}

JOB_SUCCEEDED = {
    "v": 1,
    "ok": True,
    "id": "srv-3",
    "result": {
        "id": "j-1",
        "command": "link.import",
        "state": "succeeded",
        "progress": {"done": 1, "total": 1},
        "result": {"imported": 1},
        "created_at": "2026-01-01T00:00:00Z",
    },
}


# ---------------------------------------------------------------------------
# Fake adapter
# ---------------------------------------------------------------------------


class FakeAdapter(cc.Adapter):
    """In-process adapter that replays canned envelopes (no binary).

    ``job.get`` requests are served from *job_sequence* (its last entry repeats
    once exhausted, so an endless ``running`` models a timeout); all other
    requests are served from *main* in order. Every request text is captured in
    :attr:`sent_texts` so a test can assert substitution reached the wire.
    """

    def __init__(
        self,
        transport: str = "stdio",
        main: Optional[List[Dict[str, Any]]] = None,
        job_sequence: Optional[List[Dict[str, Any]]] = None,
    ) -> None:
        self.transport = transport
        self._main = list(main or [])
        self._jobs = list(job_sequence or [])
        self._job_i = 0
        self.sent_texts: List[str] = []
        self.began = False
        self.ended = False

    def begin_case(self, case: Dict[str, Any], ctx: cc.CaseContext) -> Optional[str]:
        self.began = True
        return None

    def send(
        self, step: Dict[str, Any], request_text: str, ctx: cc.CaseContext
    ) -> Dict[str, Any]:
        self.sent_texts.append(request_text)
        request = step.get("request")
        command = request.get("command") if isinstance(request, dict) else None
        if command == "job.get":
            index = min(self._job_i, len(self._jobs) - 1)
            self._job_i += 1
            return copy.deepcopy(self._jobs[index])
        response = self._main.pop(0) if self._main else {}
        return copy.deepcopy(response)

    def end_case(self) -> None:
        self.ended = True


# ---------------------------------------------------------------------------
# Case builders
# ---------------------------------------------------------------------------


def _create_case(expect_slug: str = "docs") -> Dict[str, Any]:
    return {
        "name": "selftest_create",
        "doc": "create a link",
        "command": "link.create",
        "steps": [
            {
                "label": "create",
                "request": {
                    "v": 1,
                    "command": "link.create",
                    "args": {"target_url": "https://example.com/d", "slug": "docs"},
                },
                "expect": {
                    "ok": True,
                    "result": {
                        "slug": expect_slug,
                        "created_at": "<ts>",
                        "id": "<id>",
                    },
                    "match": "subset",
                },
            }
        ],
    }


def _import_job_case(timeout_ms: int = 5000) -> Dict[str, Any]:
    return {
        "name": "selftest_import_job",
        "doc": "async import with await",
        "command": "link.import",
        "steps": [
            {
                "label": "import",
                "request": {
                    "v": 1,
                    "command": "link.import",
                    "args": {"format": "csv", "data": "x"},
                    "options": {"async": True},
                },
                "save": {"job_id": "$.job.id"},
                "expect_accept": {
                    "ok": True,
                    "job": {"id": "<id>", "state": "queued"},
                },
                "await_job": {"var": "job_id", "timeout_ms": timeout_ms},
                "expect": {
                    "ok": True,
                    "result": {"imported": 1},
                    "match": "subset",
                },
            }
        ],
    }


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


class HarnessSelfTest(unittest.TestCase):
    """Self-test of the conformance harness ``run_case`` executor."""

    def test_pass(self) -> None:
        adapter = FakeAdapter(main=[OK_CREATE])
        findings = cc.run_case(_create_case(), adapter)
        self.assertEqual(findings, [])
        self.assertTrue(adapter.began and adapter.ended)

    def test_mismatch_reports_path(self) -> None:
        adapter = FakeAdapter(main=[OK_CREATE])
        findings = cc.run_case(_create_case(expect_slug="other"), adapter)
        self.assertTrue(findings)
        self.assertTrue(
            any("result.slug" in f for f in findings),
            f"expected a result.slug finding, got {findings!r}",
        )

    def test_explicit_skip(self) -> None:
        case = _create_case()
        case["transports"] = {"skip": {"stdio": "not supported here"}}
        adapter = FakeAdapter(transport="stdio")
        with self.assertRaises(unittest.SkipTest) as caught:
            cc.run_case(case, adapter)
        self.assertIn("not supported here", str(caught.exception))

    def test_implicit_capability_skip_is_failure(self) -> None:
        # rest_resource has no 'options' capability; a step using request.options
        # with no transports.skip entry must fail, not silently skip.
        case = _create_case()
        case["steps"][0]["request"]["options"] = {"dry_run": True}
        adapter = FakeAdapter(transport="rest_resource", main=[OK_CREATE])
        findings = cc.run_case(case, adapter)
        self.assertTrue(findings)
        self.assertTrue(
            any("options" in f for f in findings),
            f"expected an options-capability finding, got {findings!r}",
        )

    def test_job_poll_success(self) -> None:
        adapter = FakeAdapter(
            main=[ACCEPT], job_sequence=[JOB_RUNNING, JOB_SUCCEEDED]
        )
        findings = cc.run_case(_import_job_case(), adapter)
        self.assertEqual(findings, [])

    def test_job_poll_timeout(self) -> None:
        adapter = FakeAdapter(main=[ACCEPT], job_sequence=[JOB_RUNNING])
        findings = cc.run_case(_import_job_case(timeout_ms=60), adapter)
        self.assertTrue(findings)
        self.assertTrue(
            any("terminal" in f for f in findings),
            f"expected a timeout finding, got {findings!r}",
        )

    def test_variable_save_and_substitute(self) -> None:
        case = {
            "name": "selftest_vars",
            "doc": "save a slug then reuse it",
            "command": "link.get",
            "steps": [
                {
                    "label": "create",
                    "request": {
                        "v": 1,
                        "command": "link.create",
                        "args": {"target_url": "https://example.com/d", "slug": "docs"},
                    },
                    "save": {"slug": "$.result.slug"},
                    "expect": {
                        "ok": True,
                        "result": {"slug": "<id>"},
                        "match": "subset",
                    },
                },
                {
                    "label": "get",
                    "request": {
                        "v": 1,
                        "command": "link.get",
                        "args": {"slug": "${slug}"},
                    },
                    "expect": {
                        "ok": True,
                        "result": {"slug": "${slug}"},
                        "match": "subset",
                    },
                },
            ],
        }
        adapter = FakeAdapter(main=[OK_CREATE, OK_GET])
        findings = cc.run_case(case, adapter)
        self.assertEqual(findings, [])
        # The second request must have carried the saved value on the wire.
        self.assertIn("docs", adapter.sent_texts[1])

    def test_tmp_windows_path_normalized(self) -> None:
        case = {
            "name": "selftest_tmp",
            "doc": "tmp expands with forward slashes",
            "command": "link.import",
            "steps": [
                {
                    "label": "import",
                    "request": {
                        "v": 1,
                        "command": "link.import",
                        "args": {"format": "csv", "path": "${tmp}/pairs.csv"},
                    },
                    "expect": {"ok": True, "result": {}, "match": "subset"},
                }
            ],
        }
        windows_tmp = r"C:\Users\runner\conf_case_x"
        adapter = FakeAdapter(main=[OK_EMPTY])
        findings = cc.run_case(case, adapter, tmp_override=windows_tmp)
        self.assertEqual(findings, [])
        request_text = adapter.sent_texts[0]
        self.assertIn("C:/Users/runner/conf_case_x/pairs.csv", request_text)
        self.assertNotIn("\\", request_text)

    def test_build_suite_and_summary(self) -> None:
        # build_suite over a tiny on-disk corpus, then the summary writer.
        with tempfile.TemporaryDirectory() as root:
            commands = Path(root) / "commands"
            commands.mkdir()
            (commands / "00_self.json").write_text(
                json.dumps(
                    {
                        "corpus_version": 1,
                        "suite": "selftest",
                        "cases": [_create_case()],
                    }
                ),
                encoding="utf-8",
            )
            collector = cc._SummaryCollector("stdio")
            suite = cc.build_suite(
                lambda: FakeAdapter(main=[OK_CREATE]),
                "stdio",
                collector,
                corpus_dir=commands,
            )
            result = unittest.TestResult()
            suite.run(result)
            self.assertTrue(result.wasSuccessful(), result.failures + result.errors)
            self.assertEqual(len(collector.records), 1)
            self.assertEqual(collector.records[0]["status"], "pass")

            out_dir = Path(root) / "out"
            previous = os.environ.get("URLSHORTENER_CONFORMANCE_OUT")
            os.environ["URLSHORTENER_CONFORMANCE_OUT"] = str(out_dir)
            try:
                written = collector.write()
            finally:
                if previous is None:
                    os.environ.pop("URLSHORTENER_CONFORMANCE_OUT", None)
                else:
                    os.environ["URLSHORTENER_CONFORMANCE_OUT"] = previous
            self.assertIsNotNone(written)
            payload = json.loads(Path(written).read_text(encoding="utf-8"))
            self.assertEqual(payload["transport"], "stdio")
            self.assertEqual(len(payload["cases"]), 1)


if __name__ == "__main__":
    unittest.main()
