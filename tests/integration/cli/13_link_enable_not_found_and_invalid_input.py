"""
Integration test 13: link enable error-path coverage.

STORAGE-SCOPING NOTE (confirmed against src, Task 03.0 subtask 02):
  One-shot CLI mode never loads or saves `uri.txt`; DispatchLinkCommand builds
  a fresh, empty LegacyLinkStore per invocation (process-local `static`
  repository). A link cannot be seeded into the store the CLI queries - not by
  a prior CLI invocation, not by a REST server fixture (a separate process's
  repository). `enable` targets a pre-existing slug, so its happy path is
  unreachable from a single-shot CLI invocation; only not-found and input
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


class LinkEnableNotFoundAndInvalidInputTest(CliIntegrationBase):
    """
    [Integration][CLI] link enable rejects unknown slugs and missing input.

    Scenario:
        Given an empty working directory (fresh, unseeded CLI store).
        When:  url_shortener link enable --slug <various>
        Then:  a valid slug that does not exist exits 1 (not-found) with empty
               stdout and a stderr diagnostic; omitting --slug exits non-zero.

    Why this matters:
        enable flips a link back to serving redirects. A not-found enable that
        silently exited 0 would falsely imply a dormant link was reactivated,
        hiding that no such link exists.

    If this breaks, first check:
        - LinkCommandService::SetLinkEnabled not-found path -> AppErrorCode::not_found.
        - ExitCodeForAppError maps not_found -> 1 (link_command_dispatch.cpp).
        - parseSetEnabledArgs required --slug enforcement (link_command_args.cpp).
    """

    def test_enable_unknown_slug_exits_not_found(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "enable",
                "--slug", "totally-unknown-enable-slug",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 1,
                f"Expected not-found exit 1 for unknown slug; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )
            self.assertEqual(
                proc.stdout.strip(), "",
                f"not-found enable must not emit JSON on stdout; got {proc.stdout!r}",
            )
            self.assertTrue(
                proc.stderr.strip(),
                "not-found enable must emit a diagnostic on stderr",
            )

    def test_enable_unknown_slug_does_not_emit_enabled_json(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "enable",
                "--slug", "totally-unknown-enable-slug",
                cwd=tmpdir,
            )
            try:
                data = json.loads(proc.stdout.strip())
            except json.JSONDecodeError:
                return
            self.assertNotEqual(
                data.get("enabled"), True,
                f"Expected no enabled=true JSON for unknown slug; got {data}",
            )

    def test_enable_missing_slug_is_rejected(self):
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(self._binary, "link", "enable", cwd=tmpdir)
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit when --slug is missing",
            )

    def test_enable_unknown_flag_is_rejected(self):
        """An unrecognized flag must be rejected, not silently ignored."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "enable",
                "--slug", "some-slug",
                "--bogus-flag", "x",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit for an unknown flag",
            )


if __name__ == "__main__":
    unittest.main()
