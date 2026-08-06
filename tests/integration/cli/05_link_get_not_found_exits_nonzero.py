"""
Integration test 05: link get with an unknown slug exits non-zero and prints
an error indicator to stdout or stderr (not a valid link JSON).

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import json
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


class LinkGetNotFoundExitsNonzeroTest(CliIntegrationBase):
    """
    [Integration][CLI] link get with an unknown slug exits non-zero.

    Scenario:
        Given an empty working directory (no uri.txt, no links).
        When: url_shortener link get --slug totally-unknown-slug
        Then: exit code != 0, and stdout does not contain a valid link JSON
              with status == "active".

    Why this matters:
        If get silently exits 0 for a missing slug the caller cannot distinguish
        "found" from "not found" without parsing output.

    If this breaks, first check:
        - GetLink not-found path in LinkCommandService returns AppErrorCode::not_found.
        - main.cpp CLI: exits with code 1 (or non-zero) on AppError.
    """

    def test_link_get_unknown_slug_exits_nonzero(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "totally-unknown-slug",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                f"Expected non-zero exit for unknown slug.\n"
                f"stdout: {proc.stdout!r}\nstderr: {proc.stderr!r}",
            )

    def test_link_get_unknown_slug_does_not_emit_active_json(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "totally-unknown-slug",
                cwd=tmpdir,
            )
            # stdout should NOT be a JSON object with status=="active"
            try:
                data = json.loads(proc.stdout.strip())
                self.assertNotEqual(
                    data.get("status"), "active",
                    "Expected no active link JSON for unknown slug; "
                    f"got: {proc.stdout!r}",
                )
            except json.JSONDecodeError:
                pass  # Non-JSON output for not-found is also acceptable

    def test_link_get_unknown_id_exits_nonzero(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "get",
                "--id", "ffffffff-0000-0000-0000-000000000000",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit for unknown id",
            )


if __name__ == "__main__":
    unittest.main()
