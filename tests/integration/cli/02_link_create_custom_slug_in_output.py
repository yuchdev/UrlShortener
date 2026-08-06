"""
Integration test 02: link create with an explicit --slug uses that slug verbatim
in the JSON output.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli, parse_json_stdout


class LinkCreateCustomSlugTest(CliIntegrationBase):
    """
    [Integration][CLI] link create with --slug uses the slug in the response.

    Scenario:
        Given no existing state.
        When: url_shortener link create --url https://example.com --slug my-custom-slug
        Then: exit 0, JSON slug == "my-custom-slug",
              short_url ends with "/my-custom-slug".

    If this breaks, first check:
        - CLI argument parsing for --slug option.
        - CreateLinkCommand.slug forwarded from CLI args to LinkCommandService.
    """

    def test_custom_slug_appears_in_json_response(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/page",
                "--slug", "my-custom-slug",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 0,
                f"stdout: {proc.stdout!r}\nstderr: {proc.stderr!r}",
            )
            data = parse_json_stdout(proc)
            self.assertEqual(data.get("slug"), "my-custom-slug")

    def test_custom_slug_in_short_url(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/page",
                "--slug", "my-custom-slug",
                cwd=tmpdir,
            )
            data = parse_json_stdout(proc)
            self.assertTrue(
                str(data.get("short_url", "")).endswith("/my-custom-slug"),
                f"short_url '{data.get('short_url')}' does not end with /my-custom-slug",
            )

    def test_invalid_slug_format_is_rejected(self):
        """Slug with spaces or leading/trailing hyphens must be rejected (exit non-zero)."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com",
                "--slug", "slug with spaces",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit for invalid slug with spaces",
            )

    def test_reserved_slug_is_rejected(self):
        """Reserved slug 'health' must be rejected (exit non-zero)."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com",
                "--slug", "health",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit for reserved slug 'health'",
            )


if __name__ == "__main__":
    unittest.main()
