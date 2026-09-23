"""
Integration test 12: link delete error-path coverage.

STORAGE-SCOPING NOTE (confirmed against src, Task 03.0 subtask 02):
  One-shot CLI mode never loads or saves `uri.txt` and DispatchLinkCommand
  builds a fresh, empty LegacyLinkStore per invocation (process-local
  `static` repository in linkRepository()). A link cannot be seeded into the
  store the CLI queries - neither by a prior CLI invocation nor by a REST
  server fixture (a separate process's repository). `delete` acts on a
  pre-existing slug, so its happy path is unreachable from a single-shot CLI
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


class LinkDeleteNotFoundAndInvalidInputTest(CliIntegrationBase):
    """
    [Integration][CLI] link delete rejects unknown slugs and missing input.

    Scenario:
        Given an empty working directory (fresh, unseeded CLI store).
        When:  url_shortener link delete --slug <various>
        Then:  a valid slug that does not exist exits 1 (not-found) with empty
               stdout and a stderr diagnostic; omitting/empty --slug exits
               non-zero.

    Why this matters:
        delete soft-deletes a link so it stops resolving. A not-found delete
        that silently exited 0 would let a caller believe a link was retired
        when it never existed, masking a typo in the slug.

    If this breaks, first check:
        - LinkCommandService::DeleteLink not-found path -> AppErrorCode::not_found.
        - ExitCodeForAppError maps not_found -> 1 (link_command_dispatch.cpp).
        - parseDeleteArgs required --slug enforcement (link_command_args.cpp).
    """

    def test_delete_unknown_slug_exits_not_found(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "delete",
                "--slug", "totally-unknown-delete-slug",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 1,
                f"Expected not-found exit 1 for unknown slug; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )
            self.assertEqual(
                proc.stdout.strip(), "",
                f"not-found delete must not emit JSON on stdout; got {proc.stdout!r}",
            )
            self.assertTrue(
                proc.stderr.strip(),
                "not-found delete must emit a diagnostic on stderr",
            )

    def test_delete_unknown_slug_does_not_emit_deleted_json(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "delete",
                "--slug", "totally-unknown-delete-slug",
                cwd=tmpdir,
            )
            try:
                data = json.loads(proc.stdout.strip())
            except json.JSONDecodeError:
                return
            self.assertNotIn(
                data.get("status"), {"active", "deleted"},
                f"Expected no link-state JSON for unknown slug; got {data}",
            )

    def test_delete_missing_slug_is_rejected(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(self._binary, "link", "delete", cwd=tmpdir)
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit when --slug is missing",
            )

    def test_delete_empty_slug_is_rejected(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "delete",
                "--slug", "",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit for an empty --slug value",
            )


if __name__ == "__main__":
    unittest.main()
