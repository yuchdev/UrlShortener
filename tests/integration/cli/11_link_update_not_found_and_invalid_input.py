"""
Integration test 11: link update error-path coverage.

STORAGE-SCOPING NOTE (confirmed against src, Task 03.0 subtask 02):
  One-shot CLI mode never loads or saves `uri.txt` (main.cpp short-circuits
  into cli::DispatchLinkCommand *before* the server's uri.txt deserialize),
  and DispatchLinkCommand builds a brand-new LegacyLinkStore each invocation
  via app::BuildLegacyLinkCommandService, backed by the process-local
  `static InMemoryMetadataRepository` in linkRepository(). Therefore:
    * a link created by a prior CLI invocation is invisible to this one, and
    * a link seeded via a REST server fixture lives in a *different* process's
      repository the CLI never queries.
  `update` operates on a pre-existing slug, so its happy path is unreachable
  from a single-shot CLI invocation against an always-empty store. This suite
  therefore exercises the only reachable outcomes: not-found and input
  validation. See docs/roadmap/0003-cli_rest_interfaces/05.0-tests/
  03-integration-tests-for-remaining-commands.md.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import json
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


class LinkUpdateNotFoundAndInvalidInputTest(CliIntegrationBase):
    """
    [Integration][CLI] link update rejects unknown slugs and invalid input.

    Scenario:
        Given an empty working directory (fresh, unseeded CLI store).
        When:  url_shortener link update --slug <various>
        Then:  a valid slug that does not exist exits 1 (not-found) with an
               empty stdout and a diagnostic on stderr; malformed invocations
               exit non-zero without emitting a link JSON.

    Why this matters:
        update mutates a stored link's lifecycle (enabled/expiry/tags). If a
        not-found update silently exited 0, callers could not distinguish "no
        such link" from "updated", and a malformed --enabled/--expires-at that
        was silently accepted would store a lifecycle the caller never asked
        for.

    If this breaks, first check:
        - LinkCommandService::UpdateLink not-found path -> AppErrorCode::not_found.
        - ExitCodeForAppError maps not_found -> 1 (link_command_dispatch.cpp).
        - parseUpdateArgs required-flag/bool/timestamp validation
          (src/cli/link_command_args.cpp).
    """

    def test_update_unknown_slug_exits_not_found(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "update",
                "--slug", "totally-unknown-update-slug",
                "--enabled", "false",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 1,
                f"Expected not-found exit 1 for unknown slug; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )
            self.assertEqual(
                proc.stdout.strip(), "",
                f"not-found update must not emit a link JSON on stdout; "
                f"got: {proc.stdout!r}",
            )
            self.assertTrue(
                proc.stderr.strip(),
                "not-found update must emit a diagnostic on stderr",
            )

    def test_update_unknown_slug_does_not_emit_active_json(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "update",
                "--slug", "totally-unknown-update-slug",
                "--enabled", "true",
                cwd=tmpdir,
            )
            try:
                data = json.loads(proc.stdout.strip())
            except json.JSONDecodeError:
                return  # non-JSON output for not-found is acceptable
            self.assertNotEqual(
                data.get("status"), "active",
                f"Expected no active link JSON for unknown slug; got {data}",
            )

    def test_update_missing_slug_is_rejected(self):
        """Omitting the required --slug selector must exit non-zero."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "update",
                "--enabled", "false",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit when --slug is missing",
            )

    def test_update_invalid_enabled_bool_is_rejected(self):
        """A non-boolean --enabled value must be rejected before storage."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "update",
                "--slug", "some-slug",
                "--enabled", "notabool",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit for a non-boolean --enabled value",
            )
            self.assertEqual(
                proc.stdout.strip(), "",
                f"invalid input must not emit a link JSON; got {proc.stdout!r}",
            )


if __name__ == "__main__":
    unittest.main()
