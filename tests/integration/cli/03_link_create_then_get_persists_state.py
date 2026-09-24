"""
Integration test 03: CLI invocations are process-local and do not share state.

This test validates that a link created in one CLI process invocation is not
visible to a later invocation, even in the same working directory.
"""
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli, parse_json_stdout


class LinkCreateThenGetPersistsStateTest(CliIntegrationBase):
    """
    [Integration][CLI] link state is isolated per process invocation.

    Scenario:
        Given no prior state (fresh tmpdir shared by both invocations).
        When:  url_shortener link create --url https://persist.example.com
                                         --slug persist-test
               url_shortener link get --slug persist-test
        Then:  `create` exits 0 and `get` exits non-zero with not-found.

    Why this matters:
        CLI subcommands run one-shot against an in-process in-memory store.
        State must not leak across separate process invocations.
    """

    def test_create_then_get_by_slug_is_not_found_in_new_process(self):
        with self.make_tmpdir() as tmpdir:
            # Step 1: create
            create_proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://persist.example.com",
                "--slug", "persist-test",
                cwd=tmpdir,
            )
            self.assertEqual(
                create_proc.returncode, 0,
                f"create failed: stdout={create_proc.stdout!r} stderr={create_proc.stderr!r}",
            )
            create_data = parse_json_stdout(create_proc)
            self.assertEqual(create_data.get("slug"), "persist-test")

            # Step 2: get in same tmpdir but a new process invocation.
            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "persist-test",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                get_proc.returncode, 0,
                "get should fail because separate CLI invocations do not share state",
            )
            self.assertIn("Link not found", get_proc.stderr)

    def test_create_then_get_by_id_is_not_found_in_new_process(self):
        with self.make_tmpdir() as tmpdir:
            # Step 1: create
            create_proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://byid.example.com",
                "--slug", "byid-test",
                cwd=tmpdir,
            )
            self.assertEqual(create_proc.returncode, 0)
            create_data = parse_json_stdout(create_proc)
            link_id = create_data.get("id")
            self.assertTrue(link_id, "create response must include non-empty 'id'")

            # Step 2: get by id
            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--id", link_id,
                cwd=tmpdir,
            )
            self.assertNotEqual(
                get_proc.returncode, 0,
                "get by id should fail because separate CLI invocations do not share state",
            )
            self.assertIn("Link not found", get_proc.stderr)

    def test_different_tmpdirs_are_isolated(self):
        """State from one invocation does not leak into a separate tmp directory."""
        with self.make_tmpdir() as tmpdir_a, self.make_tmpdir() as tmpdir_b:
            run_cli(
                self._binary,
                "link", "create",
                "--url", "https://isolated.example.com",
                "--slug", "isolated-slug",
                cwd=tmpdir_a,
            )
            # get in a different dir: should NOT find the link from tmpdir_a
            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "isolated-slug",
                cwd=tmpdir_b,
            )
            self.assertNotEqual(
                get_proc.returncode, 0,
                "get in a fresh directory should not find a link created elsewhere",
            )


if __name__ == "__main__":
    unittest.main()
