"""
Integration test 03: CLI persistence and isolation (milestone 0006, task
02.0 subtask 13).

Two-sided contract, selected by the storage configuration:

  * WITHOUT ``--storage-config`` (and with ``URL_SHORTENER_STORAGE_CONFIG``
    scrubbed via ``clean_env()``): the default in-memory backend is
    process-local, so a link created in one CLI process is NOT visible to a
    later invocation -- the unchanged isolation contract. These cases prove
    the default did not change.

  * WITH a SQLite ``--storage-config``: separate CLI processes share one
    database file, so a link created in one process is visible to later
    ``get`` / ``disable`` / ``delete`` / ``restore`` / ``preview`` processes.

The file name is retained (it is referenced by ADR 0001, docs/cli/README.md
and the milestone docs); the class/case docstrings state exactly what is
asserted.

Env isolation (T-41): isolation cases run under ``clean_env()`` so an
inherited ``URL_SHORTENER_STORAGE_CONFIG`` cannot silently activate a
persistent backend. Temp-file isolation: every case uses a fresh
``TemporaryDirectory`` and an absolute, per-dir SQLite path, so overlapping
ctest runs never collide on a fixed database file.
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import (
    CliIntegrationBase,
    clean_env,
    parse_json_stdout,
    run_cli,
    write_sqlite_storage_yaml,
)


class LinkStateIsolationDefaultModeTest(CliIntegrationBase):
    """
    [Integration][CLI] Default (in-memory) CLI state is isolated per process.

    Scenario:
        Given no ``--storage-config`` and a scrubbed environment
            (``URL_SHORTENER_STORAGE_CONFIG`` removed).
        When:  url_shortener link create ... then, in a new process,
               url_shortener link get ...
        Then:  ``create`` exits 0 and ``get`` exits non-zero with
               ``Link not found`` -- separate invocations do not share state.

    Why this matters:
        The default CLI backend is a per-process in-memory store. T-41: a
        stray storage-config env var must not leak in and turn these into a
        persistent (false-pass/false-fail) run, so the environment is cleaned.
    """

    def test_create_then_get_by_slug_is_not_found_in_new_process(self):
        with self.make_tmpdir() as tmpdir:
            env = clean_env()
            create_proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://persist.example.com",
                "--slug", "persist-test",
                cwd=tmpdir,
                env=env,
            )
            self.assertEqual(
                create_proc.returncode, 0,
                f"create failed: stdout={create_proc.stdout!r} stderr={create_proc.stderr!r}",
            )
            create_data = parse_json_stdout(create_proc)
            self.assertEqual(create_data.get("slug"), "persist-test")

            # get in the same tmpdir but a new process invocation.
            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "persist-test",
                cwd=tmpdir,
                env=env,
            )
            self.assertNotEqual(
                get_proc.returncode, 0,
                "get should fail because separate in-memory CLI invocations do not share state",
            )
            self.assertIn("Link not found", get_proc.stderr)

    def test_create_then_get_by_id_is_not_found_in_new_process(self):
        with self.make_tmpdir() as tmpdir:
            env = clean_env()
            create_proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://byid.example.com",
                "--slug", "byid-test",
                cwd=tmpdir,
                env=env,
            )
            self.assertEqual(create_proc.returncode, 0)
            create_data = parse_json_stdout(create_proc)
            link_id = create_data.get("id")
            self.assertTrue(link_id, "create response must include non-empty 'id'")

            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--id", link_id,
                cwd=tmpdir,
                env=env,
            )
            self.assertNotEqual(
                get_proc.returncode, 0,
                "get by id should fail because separate in-memory CLI invocations do not share state",
            )
            self.assertIn("Link not found", get_proc.stderr)

    def test_different_tmpdirs_are_isolated(self):
        """State from one invocation does not leak into a separate tmp directory."""
        with self.make_tmpdir() as tmpdir_a, self.make_tmpdir() as tmpdir_b:
            env = clean_env()
            run_cli(
                self._binary,
                "link", "create",
                "--url", "https://isolated.example.com",
                "--slug", "isolated-slug",
                cwd=tmpdir_a,
                env=env,
            )
            # get in a different dir: should NOT find the link from tmpdir_a.
            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "isolated-slug",
                cwd=tmpdir_b,
                env=env,
            )
            self.assertNotEqual(
                get_proc.returncode, 0,
                "get in a fresh directory should not find a link created elsewhere",
            )


class LinkStatePersistsSqliteTest(CliIntegrationBase):
    """
    [Integration][CLI] A SQLite ``--storage-config`` persists link state
    across separate CLI processes.

    Scenario:
        Given a SQLite ``--storage-config`` whose database file is shared by
            every invocation,
        When a link is created by one process and read / mutated by later,
            independent processes,
        Then each later process observes the committed state (link found,
            lifecycle changes visible, slug uniqueness enforced), and two
            different database files stay isolated from each other.

    Why this matters:
        This is the behavioral change task 02.0 introduces: the one-shot CLI
        reads and writes the configured persistent backend. Exercises the SQL
        ``findBySlug`` / ``findById`` paths, soft-delete/restore persistence,
        and the slug-conflict path -- all across process boundaries.

    Note on fields: the full ``link get`` projection exposes lifecycle state
    via its ``status`` field (``active`` / ``disabled`` / ``deleted``); the
    discrete ``enabled`` / ``deleted_at`` fields the spec names live on the
    ``link preview`` projection, so the lifecycle case asserts both commands.
    """

    def _create(self, tmpdir, cfg, url, slug):
        """Create a link against *cfg*; assert exit 0 and return its JSON."""
        proc = run_cli(
            self._binary,
            "link", "create",
            "--url", url,
            "--slug", slug,
            "--storage-config", cfg,
            cwd=tmpdir,
        )
        self.assertEqual(
            proc.returncode, 0,
            f"create failed: stdout={proc.stdout!r} stderr={proc.stderr!r}",
        )
        return parse_json_stdout(proc)

    def test_sqlite_create_then_get_by_slug_persists(self):
        """create in one process; get --slug in a NEW process returns the same
        link (slug, url, id match the create output)."""
        with self.make_tmpdir() as tmpdir:
            cfg = write_sqlite_storage_yaml(tmpdir)
            created = self._create(
                tmpdir, cfg, "https://sqlite-slug.example.com", "sql-slug"
            )

            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "sql-slug",
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertEqual(
                get_proc.returncode, 0,
                f"get should succeed against a persistent backend: stderr={get_proc.stderr!r}",
            )
            got = parse_json_stdout(get_proc)
            self.assertEqual(got.get("slug"), created.get("slug"))
            self.assertEqual(got.get("url"), created.get("url"))
            self.assertEqual(got.get("id"), created.get("id"))
            self.assertEqual(got.get("url"), "https://sqlite-slug.example.com")

    def test_sqlite_create_then_get_by_id_persists(self):
        """create in one process; get --id in a NEW process returns the same
        link (exercises the SQL findById scan)."""
        with self.make_tmpdir() as tmpdir:
            cfg = write_sqlite_storage_yaml(tmpdir)
            created = self._create(
                tmpdir, cfg, "https://sqlite-byid.example.com", "sql-byid"
            )
            link_id = created.get("id")
            self.assertTrue(link_id, "create response must include non-empty 'id'")

            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--id", link_id,
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertEqual(
                get_proc.returncode, 0,
                f"get by id should succeed against a persistent backend: stderr={get_proc.stderr!r}",
            )
            got = parse_json_stdout(get_proc)
            self.assertEqual(got.get("id"), link_id)
            self.assertEqual(got.get("slug"), "sql-byid")
            self.assertEqual(got.get("url"), "https://sqlite-byid.example.com")

    def test_sqlite_lifecycle_persists_across_processes(self):
        """disable -> delete -> restore, each in its own process, all persist:
        after disable ``enabled == false``; after delete a ``deleted_at`` is
        set; after restore the ``deleted_at`` is cleared again."""
        with self.make_tmpdir() as tmpdir:
            cfg = write_sqlite_storage_yaml(tmpdir)
            self._create(
                tmpdir, cfg, "https://sqlite-life.example.com", "sql-life"
            )

            def preview():
                proc = run_cli(
                    self._binary, "link", "preview",
                    "--slug", "sql-life",
                    "--storage-config", cfg, cwd=tmpdir,
                )
                self.assertEqual(
                    proc.returncode, 0,
                    f"preview failed: stderr={proc.stderr!r}",
                )
                return parse_json_stdout(proc)

            def get_status():
                proc = run_cli(
                    self._binary, "link", "get",
                    "--slug", "sql-life",
                    "--storage-config", cfg, cwd=tmpdir,
                )
                self.assertEqual(proc.returncode, 0, proc.stderr)
                return parse_json_stdout(proc).get("status")

            # disable (own process) -> visible as enabled=false / status disabled.
            disable_proc = run_cli(
                self._binary, "link", "disable",
                "--slug", "sql-life",
                "--storage-config", cfg, cwd=tmpdir,
            )
            self.assertEqual(disable_proc.returncode, 0, disable_proc.stderr)
            self.assertEqual(preview().get("enabled"), False)
            self.assertEqual(get_status(), "disabled")

            # delete (own process) -> soft-delete marker persists.
            delete_proc = run_cli(
                self._binary, "link", "delete",
                "--slug", "sql-life",
                "--storage-config", cfg, cwd=tmpdir,
            )
            self.assertEqual(delete_proc.returncode, 0, delete_proc.stderr)
            after_delete = preview()
            self.assertIsNotNone(
                after_delete.get("deleted_at"),
                "deleted_at must be set after a soft delete persisted to SQLite",
            )
            self.assertEqual(get_status(), "deleted")

            # restore (own process) -> deleted_at cleared.
            restore_proc = run_cli(
                self._binary, "link", "restore",
                "--slug", "sql-life",
                "--storage-config", cfg, cwd=tmpdir,
            )
            self.assertEqual(restore_proc.returncode, 0, restore_proc.stderr)
            self.assertIsNone(
                preview().get("deleted_at"),
                "deleted_at must be cleared after restore persisted to SQLite",
            )

    def test_sqlite_two_databases_are_isolated(self):
        """A link created against database A is ``Link not found`` against a
        second, different database B."""
        with self.make_tmpdir() as tmpdir_a, self.make_tmpdir() as tmpdir_b:
            cfg_a = write_sqlite_storage_yaml(tmpdir_a)
            cfg_b = write_sqlite_storage_yaml(tmpdir_b)
            self._create(
                tmpdir_a, cfg_a, "https://db-a.example.com", "db-a-slug"
            )

            get_proc = run_cli(
                self._binary,
                "link", "get",
                "--slug", "db-a-slug",
                "--storage-config", cfg_b,
                cwd=tmpdir_b,
            )
            self.assertNotEqual(
                get_proc.returncode, 0,
                "a link in database A must not be visible through database B",
            )
            self.assertIn("Link not found", get_proc.stderr)

    def test_sqlite_duplicate_slug_conflict_across_processes(self):
        """A second create of a slug already committed by an earlier process
        fails with the slug-conflict exit code (3)."""
        with self.make_tmpdir() as tmpdir:
            cfg = write_sqlite_storage_yaml(tmpdir)
            self._create(
                tmpdir, cfg, "https://first.example.com", "dup-slug"
            )

            dup_proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://second.example.com",
                "--slug", "dup-slug",
                "--storage-config", cfg,
                cwd=tmpdir,
            )
            self.assertEqual(
                dup_proc.returncode, 3,
                "re-creating an existing slug in a new process must exit 3 "
                f"(slug_conflict); stdout={dup_proc.stdout!r} stderr={dup_proc.stderr!r}",
            )


if __name__ == "__main__":
    unittest.main()
