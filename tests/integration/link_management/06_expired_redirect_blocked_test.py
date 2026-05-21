"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class ExpiredRedirectBlockedTest(LinkManagementIntegrationBase):
    port = 38306

    def test_expired_link_redirect_blocked(self):
        """
        [Integration][Link Management] Expired links remain blocked in Stage 2.
        
        Scenario:
            Given a link created with a past expiry.
            When GET /{slug} is requested.
            Then redirect is blocked with 410 link_expired.
        
        API/Feature covered:
            - Stage 2 expiry lifecycle gate in redirect path.
        
        If this breaks, first check:
            - Expiry parsing and status resolution ordering.
        """
        status, _, _ = create_link(
            self.port,
            "lmexp01",
            "https://example.com/expired",
            '"expires_at":"2000-01-01T00:00:00Z"',
        )
        self.assertEqual(201, status)

        status, payload, _ = request(self.port, "GET", "/lmexp01")
        self.assertEqual(410, status)
        self.assertIn('"link_expired"', payload)


if __name__ == "__main__":
    unittest.main()
