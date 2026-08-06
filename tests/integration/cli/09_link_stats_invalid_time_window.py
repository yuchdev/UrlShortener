"""
Integration test 09: link stats with an invalid time window is rejected
with a non-zero exit code.

PREREQUISITE: CLI subcommand dispatch AND stats validation must be implemented.
"""
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


class LinkStatsInvalidTimeWindowTest(CliIntegrationBase):
    """
    [Integration][CLI] link stats with bad --from/--to/--bucket is rejected.

    Scenario:
        When link stats is called with clearly invalid arguments.
        Then exit code is non-zero.

    Why this matters:
        Passing garbage epoch values or an unsupported bucket granularity to
        the stats query must be caught before any database access, ensuring
        validation is client-visible and not silently ignored.

    If this breaks, first check:
        - LegacyLinkStatsReader::read() validation of from/to epoch parsing.
        - Bucket string validation ("hour" / "day" / "week" only).
        - CLI exit code mapping from AppErrorCode::invalid_field.
    """

    def test_from_greater_than_to_is_rejected(self):
        """--from later than --to is an invalid window."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "stats",
                "--slug", "any-slug",
                "--from", "1800000000",   # later than --to
                "--to",   "1700000000",
                "--bucket", "day",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                f"Expected rejection of from > to window; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )

    def test_non_numeric_from_is_rejected(self):
        """Non-numeric --from value must be rejected."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "stats",
                "--slug", "any-slug",
                "--from", "not-an-epoch",
                "--to",   "1800000000",
                "--bucket", "day",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected rejection of non-numeric --from value",
            )

    def test_invalid_bucket_granularity_is_rejected(self):
        """Unsupported --bucket value (not hour/day/week) must be rejected."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "stats",
                "--slug", "any-slug",
                "--from", "1700000000",
                "--to",   "1800000000",
                "--bucket", "minute",    # unsupported granularity
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected rejection of unsupported bucket 'minute'",
            )

    def test_missing_bucket_argument_is_rejected(self):
        """Omitting --bucket entirely must produce a non-zero exit."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "stats",
                "--slug", "any-slug",
                "--from", "1700000000",
                "--to",   "1800000000",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit when --bucket is missing",
            )


if __name__ == "__main__":
    unittest.main()
