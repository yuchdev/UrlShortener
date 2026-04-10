"""Executable integration test with self-documenting scenario and failure triage.

This file validates a single API behavior end-to-end and includes method-level
Given/When/Then coverage, API surface, and what to check first on regression.
"""
import unittest
from link_management_common import LinkManagementIntegrationBase, create_link, request


class DisableRedirectBlockedTest(LinkManagementIntegrationBase):
    port = 38302

    def test_disable_then_redirect_disabled(self):
        """
        [Integration][Link Management] Disable endpoint blocks redirects.
        
        Scenario:
            Given an active link.
            When POST /api/v1/links/{slug}/disable is called.
            Then GET /{slug} returns 410 link_disabled.
        
        API/Feature covered:
            - Stage 2 disable operation.
            - Redirect gate for disabled status.
        
        If this breaks, first check:
            - Disable state mutation persistence.
            - Redirect state resolution precedence.
        """
        status, _, _ = create_link(self.port, "lmdis01", "https://example.com/d")
        self.assertEqual(201, status)

        status, _, _ = request(self.port, "POST", "/api/v1/links/lmdis01/disable")
        self.assertEqual(200, status)

        status, payload, _ = request(self.port, "GET", "/lmdis01")
        self.assertEqual(410, status)
        self.assertIn('"link_disabled"', payload)


if __name__ == "__main__":
    unittest.main()
