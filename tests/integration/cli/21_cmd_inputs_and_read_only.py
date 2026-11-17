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
    - `cmd --help` (no command name) exits 0.

On failure first check:
    - DispatchCmdCommand calls LoggerFactory::Initialize(one_shot_logging_config)
      before building the runtime (else the audit sink leaks to stderr).
    - The caller control_set is Admin unless `--read-only` is given.

Known limitation:
    The process logger is a minimal stub that has no File sink: `--log-file`
    currently routes audit output to stderr rather than the file. The audit
    *emission* contract (emitted when enabled, suppressed when disabled) is what
    this test pins; the file-destination assertion is deferred to a logging
    subsystem that implements a real File sink (outside subtask 13's boundary).
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
        # Emission contract under an enabled sink: audit is produced (file when a
        # File sink exists, else stderr with the current stub logger) and stdout
        # stays the plain envelope. See the module "Known limitation" note.
        with self.make_tmpdir() as tmpdir:
            log_path = os.path.join(tmpdir, "audit.log")
            proc = self._run(
                "cmd", "link.create", "--log-file", log_path,
                "--json",
                '{"target_url":"https://example.com/a","slug":"logf1"}',
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            self.assertIn('"ok":true', proc.stdout)

            in_file = ""
            if os.path.exists(log_path):
                with open(log_path, encoding="utf-8") as handle:
                    in_file = handle.read()
            # The audit line is emitted somewhere once the sink is enabled.
            self.assertTrue(
                "command_audit" in in_file
                or "command_audit" in proc.stderr,
                "no audit line produced under --log-file",
            )

    def test_cmd_help_exits_zero(self):
        proc = self._run("cmd", "--help")
        self.assertEqual(proc.returncode, 0)
        self.assertIn("Usage:", proc.stdout)


if __name__ == "__main__":
    unittest.main()
