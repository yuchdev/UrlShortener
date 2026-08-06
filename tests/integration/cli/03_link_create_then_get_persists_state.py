"""
Integration test 03: link create followed by link get in the same working directory
returns the persisted link data.

This test validates the full state-persistence contract across two separate
process invocations: create stores state to disk (uri.txt) and get reads it back.

PREREQUISITE: CLI subcommand dispatch AND state persistence (uri.txt load/save)
must be implemented in main.cpp.
"""
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli, parse_json_stdout


class LinkCreateThenGetPersistsStateTest(CliIntegrationBase):
    """
    [Integration][CLI] link create persists state; link get reads it back.

    Scenario:
        Given no prior state (fresh tmpdir shared by both invocations).
        When:  url_shortener link create --url https://persist.example.com
                                         --slug persist-test
               url_shortener link get --slug persist-test
        Then:  Both exit 0. The get response has url == "https://persist.example.com"
               and slug == "persist-test".

    Why this matters:
        The CLI must persist state to disk (uri.txt) after create and load it
        before get. Without persistence, get would always return not-found.

    If this breaks, first check:
        - main.cpp CLI: serializes UriMapSingleton to uri.txt after create.
        - main.cpp CLI: deserializes uri.txt before executing get.
        - linkRepository() contains the link after create exit.
    """

    def test_create_then_get_by_slug_returns_same_link(self):
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

            # Step 2: get in same tmpdir (shares uri.txt)
            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "persist-test",
                cwd=tmpdir,
            )
            self.assertEqual(
                get_proc.returncode, 0,
                f"get failed: stdout={get_proc.stdout!r} stderr={get_proc.stderr!r}",
            )
            get_data = parse_json_stdout(get_proc)
            self.assertEqual(get_data.get("url"), "https://persist.example.com")
            self.assertEqual(get_data.get("slug"), "persist-test")

    def test_create_then_get_by_id_returns_same_link(self):
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
            self.assertEqual(
                get_proc.returncode, 0,
                f"get by id failed: stdout={get_proc.stdout!r} stderr={get_proc.stderr!r}",
            )
            get_data = parse_json_stdout(get_proc)
            self.assertEqual(get_data.get("id"), link_id)
            self.assertEqual(get_data.get("url"), "https://byid.example.com")

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
