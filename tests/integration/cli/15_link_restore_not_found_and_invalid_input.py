"""
Integration test 15: link restore error-path coverage.

STORAGE-SCOPING NOTE (confirmed against src, Task 03.0 subtask 02):
  One-shot CLI mode never loads or saves `uri.txt`; DispatchLinkCommand builds
  a fresh, empty LegacyLinkStore per invocation (process-local `static`
  repository). A soft-deleted link cannot be seeded into the store the CLI
  queries - not by a prior CLI invocation, not by a REST server fixture (a
  separate process's repository). `restore` targets a pre-existing
  (soft-deleted) slug, so its happy path is unreachable from a single-shot CLI
  invocation; only not-found and input validation are reachable. See
  docs/roadmap/0003-cli_rest_interfaces/05.0-tests/
  03-integration-tests-for-remaining-commands.md.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import json
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


class LinkRestoreNotFoundAndInvalidInputTest(CliIntegrationBase):
    """
    [Integration][CLI] link restore rejects unknown slugs and missing input.

    Scenario:
        Given an empty working directory (fresh, unseeded CLI store).
        When:  url_shortener link restore --slug <various>
        Then:  a valid slug that does not exist exits 1 (not-found) with empty
               stdout and a stderr diagnostic; omitting --slug exits non-zero.

    Why this matters:
        restore un-deletes a soft-deleted link so it serves again. A not-found
        restore that silently exited 0 would imply a retired link is live once
        more when nothing was actually restored.

    If this breaks, first check:
        - LinkCommandService::RestoreLink not-found path -> AppErrorCode::not_found.
        - ExitCodeForAppError maps not_found -> 1 (link_command_dispatch.cpp).
        - parseRestoreArgs required --slug enforcement (link_command_args.cpp).
    """

    def test_restore_unknown_slug_exits_not_found(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "restore",
                "--slug", "totally-unknown-restore-slug",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 1,
                f"Expected not-found exit 1 for unknown slug; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )
            self.assertEqual(
                proc.stdout.strip(), "",
                f"not-found restore must not emit JSON on stdout; got {proc.stdout!r}",
            )
            self.assertTrue(
                proc.stderr.strip(),
                "not-found restore must emit a diagnostic on stderr",
            )

    def test_restore_unknown_slug_does_not_emit_active_json(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "restore",
                "--slug", "totally-unknown-restore-slug",
                cwd=tmpdir,
            )
            try:
                data = json.loads(proc.stdout.strip())
            except json.JSONDecodeError:
                return
            self.assertNotEqual(
                data.get("status"), "active",
                f"Expected no active link JSON for unknown slug; got {data}",
            )

    def test_restore_missing_slug_is_rejected(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(self._binary, "link", "restore", cwd=tmpdir)
            self.assertEqual(
                proc.returncode, 1,
                "Expected exit 1 when --slug is missing",
            )

    def test_restore_unknown_flag_is_rejected(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "restore",
                "--slug", "some-slug",
                "--bogus", "x",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 1,
                "Expected exit 1 for an unknown flag",
            )


if __name__ == "__main__":
    unittest.main()
