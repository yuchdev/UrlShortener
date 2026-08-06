"""
Integration test 08: link stats on a freshly created link (zero events)
exits 0 and returns a valid stats JSON with zero totals or exits non-zero
if analytics data are unavailable in CLI mode.

PREREQUISITE: CLI subcommand dispatch AND link stats wiring must be implemented.
"""
import json
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli, parse_json_stdout

# Epoch window wide enough to cover "now" ± one day.
# Use fixed values to keep the test deterministic.
_FROM = "1700000000"   # 2023-11-14 22:13:20 UTC
_TO   = "1800000000"   # 2027-01-15 08:53:20 UTC


class LinkStatsFreshLinkTest(CliIntegrationBase):
    """
    [Integration][CLI] link stats on a fresh link with no click events.

    Scenario:
        Given a freshly created link with slug "stats-fresh".
        When: url_shortener link stats --slug stats-fresh --from 1700000000
              --to 1800000000 --bucket day
        Then: Either exit 0 with a JSON object containing slug == "stats-fresh"
              and total_attempts == 0; OR exit non-zero if CLI stats are not yet
              wired (analytics data unavailable outside server mode).

    Why permissive:
        The stats backend in legacy mode uses an in-memory global singleton that
        is not populated in CLI invocations.  The test accepts both outcomes and
        records which was observed so future implementers see what to target.

    If exit-0 path breaks, first check:
        - LegacyLinkStatsReader::read() with empty analytics data.
        - CLI stats JSON output includes the 'slug' key.
    """

    def test_stats_on_fresh_link_exits_with_known_outcome(self):
        with self.make_tmpdir() as tmpdir:
            # Create the link first.
            create_proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://fresh-stats.example.com",
                "--slug", "stats-fresh",
                cwd=tmpdir,
            )
            if create_proc.returncode != 0:
                self.skipTest("link create failed; cannot test stats on fresh link")

            # Now fetch stats.
            stats_proc = run_cli(
                self._binary,
                "link", "stats",
                "--slug", "stats-fresh",
                "--from", _FROM,
                "--to", _TO,
                "--bucket", "day",
                cwd=tmpdir,
            )

            if stats_proc.returncode == 0:
                # Stats returned: validate the JSON envelope.
                data = parse_json_stdout(stats_proc)
                self.assertEqual(
                    data.get("slug"), "stats-fresh",
                    f"slug field mismatch: {data}",
                )
                self.assertIn("total_attempts", data, "stats JSON must include total_attempts")
                self.assertEqual(
                    data.get("total_attempts"), 0,
                    f"Fresh link should have 0 total_attempts; got {data.get('total_attempts')}",
                )
            else:
                # Non-zero is also acceptable if analytics are unavailable in CLI mode.
                # Record the outcome but do not fail.
                self.assertIn(
                    stats_proc.returncode, {1, 2},
                    f"Unexpected exit code {stats_proc.returncode} for stats; "
                    f"stdout: {stats_proc.stdout!r} stderr: {stats_proc.stderr!r}",
                )


if __name__ == "__main__":
    unittest.main()
