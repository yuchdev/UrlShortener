"""
Integration test 01: link create with a valid URL prints JSON to stdout
and exits with code 0.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import json
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli, parse_json_stdout


class LinkCreateReturnsJsonTest(CliIntegrationBase):
    """
    [Integration][CLI] link create with a valid URL emits JSON and exits 0.

    Scenario:
        Given no existing state (fresh tmp directory).
        When: url_shortener link create --url https://example.com/target
              --base-domain http://sho.rt
        Then: exit code is 0, stdout is valid JSON with non-empty id, slug,
              url == "https://example.com/target", status == "active".

    If this breaks, first check:
        - main.cpp CLI dispatch: "link create" branch exits cleanly.
        - LinkCommandService::CreateLink happy path wired to CLI output.
        - serializeLinkViewJson() field completeness.
    """

    def test_link_create_exits_zero(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/target",
                "--base-domain", "http://sho.rt",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 0,
                f"Expected exit 0, got {proc.returncode}.\n"
                f"stdout: {proc.stdout!r}\nstderr: {proc.stderr!r}",
            )

    def test_link_create_stdout_is_valid_json(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/target",
                "--base-domain", "http://sho.rt",
                cwd=tmpdir,
            )
            data = parse_json_stdout(proc)
            self.assertIn("id", data, "JSON must have 'id' field")
            self.assertIn("slug", data, "JSON must have 'slug' field")

    def test_link_create_json_has_correct_url(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/target",
                "--base-domain", "http://sho.rt",
                cwd=tmpdir,
            )
            data = parse_json_stdout(proc)
            self.assertEqual(
                data.get("url"), "https://example.com/target",
                f"'url' field mismatch: {data}",
            )

    def test_link_create_json_status_is_active(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/target",
                "--base-domain", "http://sho.rt",
                cwd=tmpdir,
            )
            data = parse_json_stdout(proc)
            self.assertEqual(
                data.get("status"), "active",
                f"Expected status 'active', got: {data.get('status')}",
            )

    def test_link_create_json_short_url_uses_base_domain(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/target",
                "--base-domain", "http://sho.rt",
                cwd=tmpdir,
            )
            data = parse_json_stdout(proc)
            short_url = data.get("short_url", "")
            self.assertTrue(
                short_url.startswith("http://sho.rt/"),
                f"short_url '{short_url}' does not start with http://sho.rt/",
            )


if __name__ == "__main__":
    unittest.main()
