"""
Integration test 06: link create with an invalid target URL is rejected with
a non-zero exit code.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


class LinkCreateInvalidUrlRejectedTest(CliIntegrationBase):
    """
    [Integration][CLI] link create rejects non-HTTP(S) or malformed URLs.

    Scenario:
        When: url_shortener link create --url <bad-url>
        Then: exit code != 0 for each bad input.

    Why this matters (SSRF / target-validation):
        Any URL accepted by the CLI is stored and will later serve as a redirect
        target.  Accepting non-HTTP(S) schemes (javascript:, file:, ftp:) or
        bare hostnames can enable SSRF, phishing, or protocol confusion attacks.

    If this breaks, first check:
        - normalizeTargetUrl() rejection logic for non-http/https schemes.
        - CLI error exit code mapping from AppErrorCode::invalid_url.
    """

    _BAD_URLS = [
        "not-a-url-at-all",
        "ftp://files.example.com",
        "javascript:alert(1)",
        "file:///etc/passwd",
        "",
        "http://",          # scheme but no host
        "://missing-scheme",
    ]

    def test_invalid_urls_are_rejected(self):
        for bad_url in self._BAD_URLS:
            with self.subTest(url=bad_url):
                with self.make_tmpdir() as tmpdir:
                    args = ["link", "create"]
                    if bad_url:
                        args += ["--url", bad_url]
                    proc = run_cli(self._binary, *args, cwd=tmpdir)
                    self.assertNotEqual(
                        proc.returncode, 0,
                        f"Expected rejection of URL {bad_url!r}; "
                        f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
                    )

    def test_private_ip_target_rejected_by_default(self):
        """10.x.x.x target must be rejected unless --allow-private-targets is set."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "http://10.0.0.1/internal",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected private-IP target to be rejected by default",
            )

    def test_missing_url_argument_is_rejected(self):
        """Omitting --url entirely must produce a non-zero exit."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                cwd=tmpdir,
            )
            self.assertNotEqual(
                proc.returncode, 0,
                "Expected non-zero exit when --url is missing",
            )


if __name__ == "__main__":
    unittest.main()
