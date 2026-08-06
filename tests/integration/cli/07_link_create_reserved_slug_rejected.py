"""
Integration test 07: link create with a reserved slug is rejected with a
non-zero exit code.

PREREQUISITE: CLI subcommand dispatch must be implemented in main.cpp.
"""
import sys
import os
import unittest

sys.path.insert(0, os.path.dirname(__file__))
from cli_integration_common import CliIntegrationBase, run_cli


# These slugs map to system routes and must never be stored as user links.
RESERVED_SLUGS = [
    "api",
    "health",
    "metrics",
    "admin",
    "login",
    "logout",
    "r",
    "preview",
    "stats",
    "API",       # case-insensitive check
    "Health",
    "ADMIN",
]


class LinkCreateReservedSlugRejectedTest(CliIntegrationBase):
    """
    [Integration][CLI] link create rejects every reserved slug.

    Scenario:
        When: url_shortener link create --url https://example.com --slug <reserved>
        Then: exit code != 0 for each reserved slug.

    Why this matters:
        Reserved slugs clash with system routes (health-check, metrics, admin
        panel). Allowing them would silently mask those routes.

    If this breaks, first check:
        - isReservedSlug() denylist and case normalisation.
        - CLI error exit code mapping from AppErrorCode::reserved_slug.
    """

    def test_reserved_slugs_are_rejected(self):
        for slug in RESERVED_SLUGS:
            with self.subTest(slug=slug):
                with self.make_tmpdir() as tmpdir:
                    proc = run_cli(
                        self._binary,
                        "link", "create",
                        "--url", "https://example.com/page",
                        "--slug", slug,
                        cwd=tmpdir,
                    )
                    self.assertNotEqual(
                        proc.returncode, 0,
                        f"Expected rejection of reserved slug {slug!r}; "
                        f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
                    )

    def test_non_reserved_slug_is_accepted(self):
        """A non-reserved slug like 'summer-promo' must succeed."""
        with self.make_tmpdir() as tmpdir:
            proc = run_cli(
                self._binary,
                "link", "create",
                "--url", "https://example.com/promo",
                "--slug", "summer-promo",
                cwd=tmpdir,
            )
            self.assertEqual(
                proc.returncode, 0,
                f"Expected acceptance of slug 'summer-promo'; "
                f"stdout: {proc.stdout!r} stderr: {proc.stderr!r}",
            )


if __name__ == "__main__":
    unittest.main()
