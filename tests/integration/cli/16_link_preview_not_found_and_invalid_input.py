"""
Integration test 16: link preview error-path coverage.

STORAGE-SCOPING NOTE (confirmed against src, Task 03.0 subtask 02):
  One-shot CLI mode never loads or saves `uri.txt`; DispatchLinkCommand builds
  a fresh, empty LegacyLinkStore per invocation (process-local `static`
  repository). A link cannot be seeded into the store the CLI queries - not by
  a prior CLI invocation, not by a REST server fixture (a separate process's
  repository). `preview` reads a pre-existing slug/id, so its happy path is
  unreachable from a single-shot CLI invocation; only not-found and selector
  validation are reachable. See docs/roadmap/0003-cli_rest_interfaces/
  05.0-tests/03-integration-tests-for-remaining-commands.md.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import json
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


class LinkPreviewNotFoundAndInvalidInputTest(CliIntegrationBase):
    """
    [Integration][CLI] link preview rejects unknown links and bad selectors.

    Scenario:
        Given an empty working directory (fresh, unseeded CLI store).
        When:  url_shortener link preview (--slug <SLUG> | --id <ID>)
        Then:  a valid selector for a link that does not exist exits 1
               (not-found) with empty stdout and a stderr diagnostic; supplying
               both/neither selector exits non-zero.

    Why this matters:
        preview reports where a slug would redirect without following it - an
        operator's safety check. If a not-found preview silently exited 0 (or
        emitted a link JSON) the operator could not tell "no such link" from a
        real target, and accepting both --slug and --id would make the resolved
        target ambiguous.

    If this breaks, first check:
        - LinkCommandService::PreviewLink not-found path -> AppErrorCode::not_found.
        - ExitCodeForAppError maps not_found -> 1 (link_command_dispatch.cpp).
        - parsePreviewArgs exactly-one-of --slug/--id enforcement
          (link_command_args.cpp).
    """

    def test_preview_unknown_slug_exits_not_found(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "preview",
                "--slug", "totally-unknown-preview-slug",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 1,
                f"Expected not-found exit 1 for unknown slug; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )
            self.assertEqual(
                proc.stdout.strip(), "",
                f"not-found preview must not emit JSON on stdout; got {proc.stdout!r}",
            )
            self.assertTrue(
                proc.stderr.strip(),
                "not-found preview must emit a diagnostic on stderr",
            )

    def test_preview_unknown_id_exits_not_found(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "preview",
                "--id", "ffffffff-0000-0000-0000-000000000000",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 1,
                f"Expected not-found exit 1 for unknown id; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )

    def test_preview_both_selectors_is_rejected(self):
        """Supplying both --slug and --id is ambiguous and must be rejected."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "preview",
                "--slug", "a-slug",
                "--id", "ffffffff-0000-0000-0000-000000000000",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit when both --slug and --id are given",
            )

    def test_preview_no_selector_is_rejected(self):
        """Supplying neither --slug nor --id must be rejected."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(self._binary, "link", "preview", cwd=tmpdir)
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit when no selector is given",
            )


if __name__ == "__main__":
    unittest.main()
