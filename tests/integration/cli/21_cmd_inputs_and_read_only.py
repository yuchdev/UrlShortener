"""
Integration test 21: `cmd` input sources, authorization, and one-shot logging
(Task 04.0 subtask 13).

Type: integration (spawns the real url_shortener binary, no listener).

Scenario:
    The `cmd` adapter accepts args from `--json -` (stdin), `--json <text>` and
    `--arg k=v`; runs as the local operator (Admin, downgraded to User by
    `--read-only`); and initializes one-shot logging so the audit sink is
    silent by default (decision log D14, threat model T-80/T-81).

Boundaries:
    - `--json -` reads the piped body; `--arg` is the shell-friendly equivalent.
    - `--read-only link.create` -> exit 5 not_authorized; `system.info
      --read-only` reports read_only:true.
    - `command.list` lists the nine link commands; `db.tables` lists `links` and
      `click_events`.
    - A successful `cmd link.create` prints no audit line on stderr (default
      logging disabled); `--verbose` routes the audit line to stderr.
    - `--log-file <path>` writes exactly one `command_audit` line to the file
      and leaves stdout the plain envelope and stderr empty (real File sink).
    - `cmd --help` (no command name) exits 0.

On failure first check:
    - DispatchCmdCommand calls LoggerFactory::Initialize(one_shot_logging_config)
      before building the runtime (else the audit sink leaks to stderr).
    - The File sink is implemented in src/observability/file_sink.cpp; a failed
      open makes Initialize return false and `cmd` exit 2 without touching the
      file.
    - The caller control_set is Admin unless `--read-only` is given.
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli, clean_env


class CmdInputsAndReadOnlyTest(CliIntegrationBase):
    """[Integration][CLI] `cmd` inputs, read-only downgrade, one-shot logging."""

    def _run(self, *args, **kwargs):
        return run_cli(self._binary, *args, env=clean_env(), timeout=8,
                       **kwargs)

    def test_json_from_stdin(self):
        proc = self._run(
            "cmd", "link.create", "--json", "-",
            input='{"target_url":"https://example.com/a","slug":"stdin1"}',
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        body = json.loads(proc.stdout.strip())
        self.assertEqual(body["result"]["slug"], "stdin1")

    def test_arg_equivalent(self):
        proc = self._run(
            "cmd", "link.create",
            "--arg", "target_url=https://example.com/a",
            "--arg", "slug=arg1",
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        body = json.loads(proc.stdout.strip())
        self.assertEqual(body["result"]["slug"], "arg1")

    def test_read_only_blocks_create(self):
        proc = self._run(
            "cmd", "link.create", "--read-only",
            "--json", '{"target_url":"https://example.com/a","slug":"ro1"}',
        )
        self.assertEqual(proc.returncode, 5)
        body = json.loads(proc.stdout.strip())
        self.assertEqual(body["error"]["code"], "not_authorized")

    def test_system_info_reports_read_only(self):
        proc = self._run("cmd", "system.info", "--read-only")
        self.assertEqual(proc.returncode, 0)
        body = json.loads(proc.stdout.strip())
        self.assertTrue(body["result"]["read_only"])

    def test_command_list_has_nine_link_commands(self):
        proc = self._run("cmd", "command.list")
        self.assertEqual(proc.returncode, 0)
        names = set(json.dumps(json.loads(proc.stdout.strip())).split('"'))
        for verb in ("create", "get", "update", "delete", "enable",
                     "disable", "restore", "preview", "stats"):
            self.assertIn(f"link.{verb}", names)

    def test_db_tables_lists_known_tables(self):
        proc = self._run("cmd", "db.tables")
        self.assertEqual(proc.returncode, 0)
        table_names = {
            t["name"]
            for t in json.loads(proc.stdout.strip())["result"]["tables"]
        }
        self.assertIn("links", table_names)
        self.assertIn("click_events", table_names)

    def test_successful_create_has_empty_stderr(self):
        # C4/D14/T-80: default one-shot logging is disabled, so no command_audit
        # line leaks to stderr on a successful mutation.
        proc = self._run(
            "cmd", "link.create",
            "--json", '{"target_url":"https://example.com/a","slug":"quiet1"}',
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(proc.stderr, "")

    def test_verbose_routes_audit_to_stderr(self):
        proc = self._run(
            "cmd", "link.create", "--verbose",
            "--json", '{"target_url":"https://example.com/a","slug":"loud1"}',
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        # The envelope still lands on stdout; the audit line goes to stderr.
        self.assertIn('"ok":true', proc.stdout)
        self.assertIn("command_audit", proc.stderr)
        # Audit lines never carry an argument value (contract C6).
        self.assertNotIn("example.com", proc.stderr)

    def test_log_file_emits_audit_without_touching_stdout(self):
        # Real File sink (spec step 0 / test 21 row): the audit line lands in
        # the file exactly once, stdout is the plain envelope, stderr is empty.
        with self.make_tmpdir() as tmpdir:
            log_path = os.path.join(tmpdir, "audit.log")
            proc = self._run(
                "cmd", "link.create", "--log-file", log_path,
                "--json",
                '{"target_url":"https://example.com/a","slug":"logf1"}',
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn('"ok":true', proc.stdout)
            self.assertEqual(proc.stderr, "")

            self.assertTrue(os.path.exists(log_path))
            with open(log_path, encoding="utf-8") as handle:
                in_file = handle.read()
            audit_lines = [
                ln for ln in in_file.splitlines() if "command_audit" in ln
            ]
            self.assertEqual(len(audit_lines), 1, in_file)
            # The audit line never carries an argument value (contract C6).
            self.assertNotIn("example.com", in_file)

    def test_cmd_help_exits_zero(self):
        proc = self._run("cmd", "--help")
        self.assertEqual(proc.returncode, 0)
        self.assertIn("Usage:", proc.stdout)

    def test_link_export_waits_for_job(self):
        # 05.0/20 (decision log D13): a job response (link.export) is waited for
        # by default; the printed envelope is the terminal job.get record, and a
        # succeeded job exits 0. The fresh in-memory store has no links, so the
        # export succeeds with an empty (header-only) body.
        proc = self._run("cmd", "link.export", "--json", '{"format":"csv"}')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        body = json.loads(proc.stdout.strip())
        self.assertTrue(body["ok"])
        self.assertEqual(body["result"]["state"], "succeeded")

    def test_link_export_no_wait_prints_accepted(self):
        # `--no-wait` prints the accepted envelope (carrying the job id and its
        # non-terminal state) and exits 0 without waiting for completion.
        proc = self._run(
            "cmd", "link.export", "--no-wait", "--json", '{"format":"csv"}')
        self.assertEqual(proc.returncode, 0, proc.stderr)
        body = json.loads(proc.stdout.strip())
        self.assertIn("job", body)
        self.assertIn("id", body["job"])
        self.assertIn(
            body["job"]["state"], ("queued", "running", "succeeded"))

    def test_link_import_from_path_waits(self):
        # Carry-forward (T-95 opt-in): the local-operator `cmd` runtime grants
        # the file-path capability, so `link.import` may read a local `path`; the
        # import runs as a job that `cmd` waits for and reports succeeded.
        with self.make_tmpdir() as tmpdir:
            csv_path = os.path.join(tmpdir, "import.csv")
            with open(csv_path, "w", encoding="utf-8") as handle:
                handle.write("https://example.com/p1,imp1\n")
                handle.write("https://example.com/p2,imp2\n")
            proc = self._run(
                "cmd", "link.import", "--json",
                json.dumps({"format": "csv", "path": csv_path}))
            self.assertEqual(proc.returncode, 0, proc.stderr)
            body = json.loads(proc.stdout.strip())
            self.assertTrue(body["ok"])
            self.assertEqual(body["result"]["state"], "succeeded")


if __name__ == "__main__":
    unittest.main()
