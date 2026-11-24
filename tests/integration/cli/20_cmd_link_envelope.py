"""
Integration test 20: `cmd <name>` prints the response envelope on stdout and
returns the shared-table exit code (Task 04.0 subtask 13).

Type: integration (spawns the real url_shortener binary, no listener).

Scenario:
    `url_shortener cmd link.create --json '{...}'` executes one command through
    the shared registry as the local operator and prints the v1 response
    envelope on one stdout line, then exits with the shared error-table code.

Boundaries:
    - Success: `link.create` -> exit 0, one-line JSON envelope with ok:true and
      result.slug; `--raw` prints only the result object.
    - not_found: `link.get` of an absent slug -> exit 1.
    - bad input: invalid URL -> exit 2 with the envelope on stdout; unknown
      command -> exit 2.
    - The process exits promptly (no HTTP listener is started; same technique
      as 04_cli_exits_no_listening_port.py).

On failure first check:
    - main.cpp dispatches `parsed.cmd` to DispatchCmdCommand before any
      io_context/HttpServer is constructed (C5).
    - Exit codes come only from commands::exit_code_for.
"""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli, clean_env


class CmdLinkEnvelopeTest(CliIntegrationBase):
    """[Integration][CLI] `cmd` prints the envelope and exits with the table."""

    def _run(self, *args, **kwargs):
        return run_cli(self._binary, *args, env=clean_env(), timeout=8,
                       **kwargs)

    def test_create_success_envelope(self):
        proc = self._run(
            "cmd", "link.create",
            "--json", '{"target_url":"https://example.com/a","slug":"env1"}',
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        body = json.loads(proc.stdout.strip())
        self.assertTrue(body["ok"])
        self.assertEqual(body["result"]["slug"], "env1")
        # D14/C4: a successful mutation emits no audit line on stderr.
        self.assertEqual(proc.stderr, "")

    def test_create_raw_prints_only_result(self):
        proc = self._run(
            "cmd", "link.create", "--raw",
            "--json", '{"target_url":"https://example.com/a","slug":"env2"}',
        )
        self.assertEqual(proc.returncode, 0, proc.stderr)
        result = json.loads(proc.stdout.strip())
        # --raw yields the bare result object: a slug, no envelope "ok" field.
        self.assertEqual(result["slug"], "env2")
        self.assertNotIn("ok", result)

    def test_get_unknown_slug_exits_one(self):
        proc = self._run("cmd", "link.get", "--arg", "slug=env-absent")
        self.assertEqual(proc.returncode, 1)
        body = json.loads(proc.stdout.strip())
        self.assertFalse(body["ok"])
        self.assertEqual(body["error"]["code"], "not_found")

    def test_invalid_url_exits_two_with_envelope(self):
        proc = self._run(
            "cmd", "link.create",
            "--json", '{"target_url":"ht!tp://bad","slug":"env3"}',
        )
        self.assertEqual(proc.returncode, 2)
        body = json.loads(proc.stdout.strip())
        self.assertFalse(body["ok"])
        self.assertEqual(body["error"]["code"], "invalid_url")

    def test_unknown_command_exits_two(self):
        proc = self._run("cmd", "no.such_command")
        self.assertEqual(proc.returncode, 2)
        body = json.loads(proc.stdout.strip())
        self.assertEqual(body["error"]["code"], "unknown_command")

    def test_process_exits_promptly(self):
        # No listener is started: the call returns well within the timeout.
        proc = self._run("cmd", "system.info")
        self.assertEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main()
